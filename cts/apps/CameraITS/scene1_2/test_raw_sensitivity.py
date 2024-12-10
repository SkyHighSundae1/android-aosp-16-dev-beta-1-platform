# Copyright 2014 The Android Open Source Project
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
"""Verifies sensitivities on RAW images."""


import logging
import math
import os.path
import matplotlib
from matplotlib import pylab
from mobly import test_runner
import numpy as np

import its_base_test
import camera_properties_utils
import capture_request_utils
import image_processing_utils
import its_session_utils

_BLACK_LEVEL_RTOL = 0.005  # 0.5%
_GR_PLANE_IDX = 1  # GR plane index in RGGB data
_IMG_STATS_GRID = 9  # Center 11.11%
_NAME = os.path.splitext(os.path.basename(__file__))[0]
_NUM_FRAMES = 4
_NUM_SENS_STEPS = 5
_VAR_THRESH = 1.01  # Each shot must be 1% noisier than previous


def define_raw_stats_fmt(props):
  """Define format with active array width and height."""
  aaw = (props['android.sensor.info.preCorrectionActiveArraySize']['right'] -
         props['android.sensor.info.preCorrectionActiveArraySize']['left'])
  aah = (props['android.sensor.info.preCorrectionActiveArraySize']['bottom'] -
         props['android.sensor.info.preCorrectionActiveArraySize']['top'])
  logging.debug('Active array W,H: %d,%d', aaw, aah)
  return {'format': 'rawStats',
          'gridWidth': aaw // _IMG_STATS_GRID,
          'gridHeight': aah // _IMG_STATS_GRID}


class RawSensitivityTest(its_base_test.ItsBaseTest):
  """Capture a set of raw images with increasing gains and measure the noise."""

  def test_raw_sensitivity(self):
    logging.debug('Starting %s', _NAME)
    with its_session_utils.ItsSession(
        device_id=self.dut.serial,
        camera_id=self.camera_id,
        hidden_physical_id=self.hidden_physical_id) as cam:
      props = cam.get_camera_properties()
      props = cam.override_with_hidden_physical_camera_props(props)
      camera_properties_utils.skip_unless(
          camera_properties_utils.raw16(props) and
          camera_properties_utils.manual_sensor(props) and
          camera_properties_utils.read_3a(props) and
          camera_properties_utils.per_frame_control(props) and
          not camera_properties_utils.mono_camera(props))
      name_with_log_path = os.path.join(self.log_path, _NAME)

      # Load chart for scene
      its_session_utils.load_scene(
          cam, props, self.scene, self.tablet,
          its_session_utils.CHART_DISTANCE_NO_SCALING)

      # Expose for the scene with min sensitivity
      sens_min, _ = props['android.sensor.info.sensitivityRange']
      # Digital gains might not be visible on RAW data
      sens_max = props['android.sensor.maxAnalogSensitivity']
      sens_step = (sens_max - sens_min) // _NUM_SENS_STEPS

      # Intentionally blur images for noise measurements
      s_ae, e_ae, _, _, _ = cam.do_3a(do_af=False, get_results=True)
      s_e_prod = s_ae * e_ae

      # Get property constants for captures
      cfa_idxs = image_processing_utils.get_canonical_cfa_order(props)
      black_levels = image_processing_utils.get_black_levels(props)
      white_level = props['android.sensor.info.whiteLevel']

      sensitivities = list(range(sens_min, sens_max, sens_step))
      variances = []
      for s in sensitivities:
        e = int(s_e_prod / float(s))
        req = capture_request_utils.manual_capture_request(s, e, 0)

        # Capture in rawStats to reduce test run time
        fmt = define_raw_stats_fmt(props)
        caps = cam.do_capture([req]*_NUM_FRAMES, fmt)
        image_processing_utils.assert_capture_width_and_height(
            caps[0], _IMG_STATS_GRID, _IMG_STATS_GRID
        )

        # Measure mean & variance
        for i, cap in enumerate(caps):
          mean_img, var_img = image_processing_utils.unpack_rawstats_capture(
              cap
          )
          mean = mean_img[_IMG_STATS_GRID//2, _IMG_STATS_GRID//2,
                          cfa_idxs[_GR_PLANE_IDX]]
          var = var_img[_IMG_STATS_GRID//2, _IMG_STATS_GRID//2,
                        cfa_idxs[_GR_PLANE_IDX]]/white_level**2
          logging.debug('cap: %d, mean: %.2f, var: %e', i, mean, var)
          variances.append(var)

      # Flag dark images
      if math.isclose(mean, max(black_levels), rel_tol=_BLACK_LEVEL_RTOL):
        raise AssertionError(f'Images are too dark! Center mean: {mean:.2f}')

      # Create plot
      sensitivities = np.repeat(sensitivities, _NUM_FRAMES)
      pylab.figure(_NAME)
      pylab.plot(sensitivities, variances, '-ro')
      pylab.xticks(sensitivities)
      pylab.xlabel('Sensitivities')
      pylab.ylabel('Image Center Patch Variance')
      pylab.ticklabel_format(axis='y', style='sci', scilimits=(-6, -6))
      pylab.title(_NAME)
      matplotlib.pyplot.savefig(f'{name_with_log_path}_variances.png')

      # Find average variance at each step
      vars_step_means = []
      for i in range(_NUM_SENS_STEPS):
        vars_step = []
        for j in range(_NUM_FRAMES):
          vars_step.append(variances[_NUM_FRAMES * i + j])
        vars_step_means.append(np.mean(vars_step))
      logging.debug('averaged variances: %s', vars_step_means)

      # Assert each set of shots is noisier than previous and save img on FAIL
      for variance_idx, variance in enumerate(vars_step_means[:-1]):
        if variance >= vars_step_means[variance_idx+1] / _VAR_THRESH:
          image_processing_utils.capture_scene_image(
              cam, props, name_with_log_path
          )
          raise AssertionError(
              f'variances [i]: {variances[variance_idx]:.5f}, '
              f'[i+1]: {variances[variance_idx+1]:.5f}, THRESH: {_VAR_THRESH}'
          )

if __name__ == '__main__':
  test_runner.main()
