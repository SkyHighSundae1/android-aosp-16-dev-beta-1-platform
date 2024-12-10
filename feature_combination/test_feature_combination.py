# Copyright 2024 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Verify feature combinations for stabilization, 10-bit, and frame rate."""

import concurrent.futures
import logging
import os

from mobly import test_runner

import its_base_test
import camera_properties_utils
import capture_request_utils
import its_session_utils
import preview_processing_utils
import video_processing_utils

_BIT_HLG10 = 0x01  # bit 1 for feature mask
_BIT_STABILIZATION = 0x02  # bit 2 for feature mask
_FPS_30_60 = (30, 60)
_FPS_SELECTION_ATOL = 0.01
_FPS_ATOL_CODEC = 1.2
_FPS_ATOL_METADATA = 0.8

_NAME = os.path.splitext(os.path.basename(__file__))[0]
_SEC_TO_NSEC = 1_000_000_000


class FeatureCombinationTest(its_base_test.ItsBaseTest):
  """Tests camera feature combinations.

  The combination of camera features tested by this function are:
  - Preview stabilization
  - Target FPS range
  - HLG 10-bit HDR

  Camera is moved in sensor fusion rig on an arc of 15 degrees.
  Speed is set to mimic hand movement (and not be too fast).
  Preview is captured after rotation rig starts moving and the
  gyroscope data is dumped.

  Preview stabilization:
  The recorded preview is processed to dump all of the frames to
  PNG files. Camera movement is extracted from frames by determining
  max angle of deflection in video movement vs max angle of deflection
  in gyroscope movement. Test is a PASS if rotation is reduced in video.

  Target FPS range:
  The recorded preview has the expected fps range. For example,
  if [60, 60] is set as targetFpsRange, the camera device is expected to
  produce 60fps preview/video.

  HLG 10-bit HDR:
  The recorded preview has valid 10-bit HLG outputs.
  """

  def test_feature_combination(self):
    with concurrent.futures.ThreadPoolExecutor() as executor:
      self._test_feature_combination(executor)

  def _test_feature_combination(self, executor):
    rot_rig = {}
    log_path = self.log_path

    with its_session_utils.ItsSession(
        device_id=self.dut.serial,
        camera_id=self.camera_id) as cam:

      # Skip if the device doesn't support feature combination query
      props = cam.get_camera_properties()
      feature_combination_query_version = props.get(
          'android.info.sessionConfigurationQueryVersion')
      if not feature_combination_query_version:
        feature_combination_query_version = (
            its_session_utils.ANDROID14_API_LEVEL
        )
      should_run = (feature_combination_query_version >=
                    its_session_utils.ANDROID15_API_LEVEL)
      camera_properties_utils.skip_unless(should_run)

      # Log ffmpeg version being used
      video_processing_utils.log_ffmpeg_version()

      # Raise error if not FRONT or REAR facing camera
      facing = props['android.lens.facing']
      camera_properties_utils.check_front_or_rear_camera(props)

      # Initialize rotation rig
      rot_rig['cntl'] = self.rotator_cntl
      rot_rig['ch'] = self.rotator_ch
      if rot_rig['cntl'].lower() != 'arduino':
        raise AssertionError(
            f'You must use the arduino controller for {_NAME}.')

      # List of queryable stream combinations
      combinations_str, combinations = cam.get_queryable_stream_combinations()
      logging.debug('Queryable stream combinations: %s', combinations_str)

      # Stabilization modes. Make sure to test ON first.
      stabilization_params = []
      stabilization_modes = props[
          'android.control.availableVideoStabilizationModes']
      if (camera_properties_utils.STABILIZATION_MODE_PREVIEW in
          stabilization_modes):
        stabilization_params.append(
            camera_properties_utils.STABILIZATION_MODE_PREVIEW)
      stabilization_params.append(
          camera_properties_utils.STABILIZATION_MODE_OFF
      )
      logging.debug('stabilization modes: %s', stabilization_params)

      configs = props['android.scaler.streamConfigurationMap'][
          'availableStreamConfigurations']
      fps_ranges = camera_properties_utils.get_ae_target_fps_ranges(props)

      test_failures = []
      preview_verification_futures = []
      combination_names = []
      for stream_combination in combinations:
        streams_name = stream_combination['name']
        min_frame_duration = 0
        configured_streams = []
        skip = False
        if (stream_combination['combination'][0]['format'] !=
            its_session_utils.PRIVATE_FORMAT):
          raise AssertionError(
              f'First stream for {streams_name} must be PRIV')
        preview_size = stream_combination['combination'][0]['size']
        for stream in stream_combination['combination']:
          fmt = None
          size = [int(e) for e in stream['size'].split('x')]
          if stream['format'] == its_session_utils.PRIVATE_FORMAT:
            fmt = capture_request_utils.FMT_CODE_PRIV
          elif stream['format'] == 'jpeg':
            fmt = capture_request_utils.FMT_CODE_JPEG
          elif stream['format'] == its_session_utils.JPEG_R_FMT_STR:
            fmt = capture_request_utils.FMT_CODE_JPEG_R
          config = [x for x in configs if
                    x['format'] == fmt and
                    x['width'] == size[0] and
                    x['height'] == size[1]]
          if not config:
            logging.debug(
                'stream combination %s not supported. Skip', streams_name)
            skip = True
            break

          min_frame_duration = max(
              config[0]['minFrameDuration'], min_frame_duration)
          logging.debug(
              'format is %s, min_frame_duration is %d}',
              stream['format'], config[0]['minFrameDuration'])
          configured_streams.append(
              {'format': stream['format'], 'width': size[0], 'height': size[1]})

        if skip:
          continue

        # Fps ranges
        max_achievable_fps = _SEC_TO_NSEC / min_frame_duration
        fps_params = [fps for fps in fps_ranges if (
            fps[1] in _FPS_30_60 and
            max_achievable_fps >= fps[1] - _FPS_SELECTION_ATOL)]

        for fps_range in fps_params:
          # HLG10. Make sure to test ON first.
          hlg10_params = []
          if cam.is_hlg10_recording_supported_for_size_and_fps(
              preview_size, fps_range[1]):
            hlg10_params.append(True)
          hlg10_params.append(False)

          features_tested = []  # feature combinations already tested
          for hlg10 in hlg10_params:
            # Construct output surfaces
            output_surfaces = []
            for configured_stream in configured_streams:
              hlg10_stream = (configured_stream['format'] ==
                              its_session_utils.PRIVATE_FORMAT and hlg10)
              output_surfaces.append({'format': configured_stream['format'],
                                      'width': configured_stream['width'],
                                      'height': configured_stream['height'],
                                      'hlg10': hlg10_stream})

            for stabilize in stabilization_params:
              settings = {
                  'android.control.videoStabilizationMode': stabilize,
                  'android.control.aeTargetFpsRange': fps_range,
              }
              combination_name = (f'(streams: {streams_name}, hlg10: {hlg10}, '
                                  f'stabilization: {stabilize}, fps_range: '
                                  f'[{fps_range[0]}, {fps_range[1]}])')
              logging.debug('combination name: %s', combination_name)

              # Is the feature combination supported?
              supported = cam.is_stream_combination_supported(
                  output_surfaces, settings)
              if not supported:
                logging.debug('%s not supported', combination_name)
                break

              is_stabilized = False
              if (stabilize ==
                  camera_properties_utils.STABILIZATION_MODE_PREVIEW):
                is_stabilized = True

              # If a superset of features are already tested, skip.
              skip_test = its_session_utils.check_and_update_features_tested(
                  features_tested, hlg10, is_stabilized)
              if skip_test:
                continue

              # TODO: b/341299485 - parallelize preview recording
              recording_obj = (
                  preview_processing_utils.collect_data_with_surfaces(
                      cam, self.tablet_device, output_surfaces, is_stabilized,
                      rot_rig=rot_rig, fps_range=fps_range))

              if is_stabilized:
                # Get gyro events
                logging.debug('Reading out inertial sensor events')
                gyro_events = cam.get_sensor_events()['gyro']
                logging.debug('Number of gyro samples %d', len(gyro_events))

              # Grab the video from the file location on DUT
              self.dut.adb.pull([recording_obj['recordedOutputPath'], log_path])

              # Verify FPS by inspecting the video clip
              preview_file_name = (
                  recording_obj['recordedOutputPath'].split('/')[-1])
              preview_file_name_with_path = os.path.join(
                  self.log_path, preview_file_name)
              average_frame_rate_codec = (
                  video_processing_utils.get_average_frame_rate(
                      preview_file_name_with_path))
              logging.debug('Average codec frame rate for %s is %f', combination_name,
                            average_frame_rate_codec)
              if (average_frame_rate_codec > fps_range[1] + _FPS_ATOL_CODEC or
                  average_frame_rate_codec < fps_range[0] - _FPS_ATOL_CODEC):
                failure_msg = (
                    f'{combination_name}: Average video clip frame rate '
                    f'{average_frame_rate_codec} exceeding the allowed range of '
                    f'({fps_range[0]}-{_FPS_ATOL_CODEC}, '
                    f'{fps_range[1]}+{_FPS_ATOL_CODEC})')
                test_failures.append(failure_msg)

              # Verify FPS by inspecting the result metadata
              capture_results = recording_obj['captureMetadata'];
              assert len(capture_results) > 1
              last_t = capture_results[-1]['android.sensor.timestamp'];
              first_t = capture_results[0]['android.sensor.timestamp'];
              average_frame_duration = (last_t - first_t) / (len(capture_results) - 1)
              average_frame_rate_metadata = _SEC_TO_NSEC / average_frame_duration
              logging.debug('Average metadata frame rate for %s is %f', combination_name,
                            average_frame_rate_metadata)
              if (average_frame_rate_metadata > fps_range[1] + _FPS_ATOL_METADATA or
                  average_frame_rate_metadata < fps_range[0] - _FPS_ATOL_METADATA):
                failure_msg = (
                    f'{combination_name}: Average frame rate '
                    f'{average_frame_rate_metadata} exceeding the allowed range of '
                    f'({fps_range[0]}-{_FPS_ATOL_METADATA}, {fps_range[1]}+{_FPS_ATOL_METADATA})')
                test_failures.append(failure_msg)

              # Schedule stabilization verification to run asynchronously
              if is_stabilized:
                future = executor.submit(
                    preview_processing_utils.verify_preview_stabilization,
                    recording_obj, gyro_events, _NAME, log_path, facing)
                preview_verification_futures.append(future)
                combination_names.append(combination_name)

              # Verify color space
              color_space = video_processing_utils.get_video_colorspace(
                  self.log_path, preview_file_name_with_path)
              if (hlg10 and
                  video_processing_utils.COLORSPACE_HDR not in color_space):
                failure_msg = (
                    f'{combination_name}: video color space {color_space} '
                    'is missing COLORSPACE_HDR')
                test_failures.append(failure_msg)

      # Verify preview stabilization
      for future, name in zip(preview_verification_futures, combination_names):
        stabilization_result = future.result()
        logging.debug('Stabilization result for %s: %s',
                      name, stabilization_result)
        if stabilization_result['failure']:
          failure_msg = f'{name}: {stabilization_result["failure"]}'
          test_failures.append(failure_msg)

      # Assert PASS/FAIL criteria
      if test_failures:
        raise AssertionError(test_failures)

if __name__ == '__main__':
  test_runner.main()
