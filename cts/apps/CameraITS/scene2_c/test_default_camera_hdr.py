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
"""Check if the default camera app capture is Ultra HDR or not.
"""
import logging
import os

from mobly import test_runner

import its_base_test
import camera_properties_utils
import its_device_utils
import its_session_utils
import ui_interaction_utils
from snippet_uiautomator import uiautomator


class DefaultCapturePerfClassTest(its_base_test.ItsBaseTest):
  """Checks if the default camera capture is Ultra HDR or not.

  Test default camera capture is Ultra HDR for VIC performance class as
  specified in CDD.

  [2.2.7.2/7.5/H-1-20] MUST by default output JPEG_R for the primary rear
  and primary front cameras in the default camera app.
  """

  def setup_class(self):
    super().setup_class()
    self.dut.services.register(
        uiautomator.ANDROID_SERVICE_NAME, uiautomator.UiAutomatorService
    )

  def on_fail(self, record):
    super().on_fail(record)
    self.dut.take_screenshot(self.log_path, prefix='on_test_fail')

  def test_default_camera_launch(self):
    with its_session_utils.ItsSession(
        device_id=self.dut.serial,
        camera_id=self.camera_id,
        hidden_physical_id=self.hidden_physical_id) as cam:

      device_id = self.dut.serial
      # Check SKIP conditions
      first_api_level = its_session_utils.get_first_api_level(self.dut.serial)
      camera_properties_utils.skip_unless(
          first_api_level >= its_session_utils.ANDROID15_API_LEVEL and
          cam.is_primary_camera())

      # Load chart for scene
      props = cam.get_camera_properties()
      its_session_utils.load_scene(
          cam, props, self.scene, self.tablet, self.chart_distance)

      lens_facing = props['android.lens.facing']
      if lens_facing == camera_properties_utils.LENS_FACING['FRONT']:
        camera_facing = 'front'
      else:
        camera_facing = 'rear'
      logging.debug('Camera facing: %s', camera_facing)
      # Get default camera app pkg name
      pkg_name = cam.get_default_camera_pkg()
      logging.debug('Default camera pkg name: %s', pkg_name)

      ui_interaction_utils.default_camera_app_dut_setup(device_id, pkg_name)

      # Launch ItsTestActivity
      its_device_utils.start_its_test_activity(device_id)
      device_img_path = ui_interaction_utils.launch_and_take_capture(
          self.dut, pkg_name, camera_facing, self.log_path)
      ui_interaction_utils.pull_img_files(
          device_id, device_img_path, self.log_path)

      # Analyze the captured image
      gainmap_present = cam.check_gain_map_present(device_img_path)
      logging.debug('gainmap_present: %s', gainmap_present)

      # Log has_gainmap so that the corresponding MPC level can be written
      # to report log. Text must match HAS_GAINMAP_PATTERN in
      # ItsTestActivity.java.
      # Note: Do not change from print to logging.
      print(f'has_gainmap:{gainmap_present}')

      # Assert gainmap_present if device claims performance class
      if (cam.is_vic_performance_class and not gainmap_present):
        raise AssertionError(f'has_gainmap: {gainmap_present}')

if __name__ == '__main__':
  test_runner.main()
