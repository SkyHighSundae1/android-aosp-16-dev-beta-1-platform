# Copyright 2016 The Android Open Source Project
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
"""Verify capture burst of full size images is fast enough to not timeout."""

import logging
import os

from mobly import test_runner

import its_base_test
import camera_properties_utils
import capture_request_utils
import image_processing_utils
import its_session_utils

_FRAME_TIME_DELTA_RTOL = 0.1  # allow 10% variation from reported value
_NAME = os.path.splitext(os.path.basename(__file__))[0]
_NR_MODE_FAST = 1  # burst capture uses noise reduction mode FAST
_NUM_TEST_FRAMES = 15
_PATCH_H = 0.1  # center 10% patch params
_PATCH_W = 0.1
_PATCH_X = 0.5 - _PATCH_W/2
_PATCH_Y = 0.5 - _PATCH_H/2
_START_FRAME = 2  # allow 1st frame to have some push out (see test_jitter.py)
_THRESH_MIN_LEVEL = 0.1  # check images aren't too dark


class BurstCaptureTest(its_base_test.ItsBaseTest):
  """Test capture a burst of full size images is fast enough and doesn't timeout.

  This test verifies that the entire capture pipeline can keep up the speed of
  fullsize capture + CPU read for at least some time.
  """

  def test_burst_capture(self):
    with its_session_utils.ItsSession(
        device_id=self.dut.serial,
        camera_id=self.camera_id,
        hidden_physical_id=self.hidden_physical_id) as cam:
      props = cam.get_camera_properties()
      props = cam.override_with_hidden_physical_camera_props(props)

      # Check SKIP conditions
      camera_properties_utils.skip_unless(
          camera_properties_utils.backward_compatible(props) and
          camera_properties_utils.burst_capture_capable
      )

      # Load chart for scene
      its_session_utils.load_scene(
          cam, props, self.scene, self.tablet, self.chart_distance)

      req = capture_request_utils.auto_capture_request()
      if camera_properties_utils.noise_reduction_mode(props, _NR_MODE_FAST):
        req['android.noiseReduction.mode'] = _NR_MODE_FAST
      cam.do_3a()
      caps = cam.do_capture([req] * _NUM_TEST_FRAMES)
      img = image_processing_utils.convert_capture_to_rgb_image(
          caps[0], props=props)
      name_with_log_path = os.path.join(self.log_path, _NAME)
      image_processing_utils.write_image(img, f'{name_with_log_path}.jpg')
      logging.debug('Image W, H: %d, %d', caps[0]['width'], caps[0]['height'])

      # Confirm center patch brightness
      patch = image_processing_utils.get_image_patch(
          img, _PATCH_X, _PATCH_Y, _PATCH_W, _PATCH_H)
      r, g, b = image_processing_utils.compute_image_means(patch)
      logging.debug('RGB levels %.3f, %.3f, %.3f', r, g, b)
      if g < _THRESH_MIN_LEVEL:
        raise AssertionError(f'Image is too dark! G center patch avg: {g:.3f}, '
                             f'THRESH: {_THRESH_MIN_LEVEL}')

      # Check frames are consecutive
      error_msg = []
      first_api_level = its_session_utils.get_first_api_level(self.dut.serial)
      frame_time_duration_deltas = []
      if first_api_level >= its_session_utils.ANDROID15_API_LEVEL:
        frame_times = [cap['metadata']['android.sensor.timestamp']
                       for cap in caps]
        for i, time in enumerate(frame_times):
          if i < _START_FRAME:
            continue
          frame_time_delta = time - frame_times[i-1]
          frame_duration = caps[i]['metadata']['android.sensor.frameDuration']
          logging.debug('cap %d frameDuration: %d ns', i, frame_duration)
          frame_time_delta_atol = frame_duration * (1+_FRAME_TIME_DELTA_RTOL)
          frame_time_duration_deltas.append(frame_time_delta - frame_duration)
          logging.debug(
              'frame_time-frameDuration: %d ns', frame_time_delta-frame_duration
          )
          if frame_time_delta > frame_time_delta_atol:
            error_msg.append(
                f'Frame {i-1} --> {i} delta: {frame_time_delta}, '
                f'ATOL: {frame_time_delta_atol:.1f} ns. '
            )
        # Note: Do not change from print to logging. print used for data-mining
        print(
            f'{_NAME}_max_frame_time_minus_frameDuration_ns: '
            f'{max(frame_time_duration_deltas)}'
        )
        if error_msg:
          raise AssertionError(f'Frame drop(s)! {error_msg}')


if __name__ == '__main__':
  test_runner.main()
