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
"""Utility functions for interacting with a device via the UI."""

import datetime
import logging
import re
import time
import types
import xml.etree.ElementTree as et

import camera_properties_utils
import its_device_utils

_DIR_EXISTS_TXT = 'Directory exists'
_PERMISSIONS_LIST = ('CAMERA', 'RECORD_AUDIO', 'ACCESS_FINE_LOCATION',
                     'ACCESS_COARSE_LOCATION')

ACTION_ITS_DO_JCA_CAPTURE = (
    'com.android.cts.verifier.camera.its.ACTION_ITS_DO_JCA_CAPTURE'
)
ACTIVITY_WAIT_TIME_SECONDS = 5
AGREE_BUTTON = 'Agree and continue'
CANCEL_BUTTON_TXT = 'Cancel'
CAMERA_FILES_PATHS = ('/sdcard/DCIM/Camera',
                      '/storage/emulated/0/Pictures')
CAPTURE_BUTTON_RESOURCE_ID = 'CaptureButton'
DONE_BUTTON_TXT = 'Done'
FLASH_MODE_TO_CLICKS = types.MappingProxyType({
    'OFF': 3,
    'AUTO': 2
})
IMG_CAPTURE_CMD = 'am start -a android.media.action.IMAGE_CAPTURE'
ITS_ACTIVITY_TEXT = 'Camera ITS Test'
JPG_FORMAT_STR = '.jpg'
OK_BUTTON_TXT = 'OK'
TAKE_PHOTO_CMD = 'input keyevent KEYCODE_CAMERA'
QUICK_SETTINGS_RESOURCE_ID = 'QuickSettingsDropDown'
QUICK_SET_FLASH_RESOURCE_ID = 'QuickSettingsFlashButton'
QUICK_SET_FLIP_CAMERA_RESOURCE_ID = 'QuickSettingsFlipCameraButton'
REMOVE_CAMERA_FILES_CMD = 'rm '
UI_DESCRIPTION_BACK_CAMERA = 'Back Camera'
UI_DESCRIPTION_FRONT_CAMERA = 'Front Camera'
UI_OBJECT_WAIT_TIME_SECONDS = datetime.timedelta(seconds=3)
VIEWFINDER_NOT_VISIBLE_PREFIX = 'viewfinder_not_visible'
VIEWFINDER_VISIBLE_PREFIX = 'viewfinder_visible'
WAIT_INTERVAL_FIVE_SECONDS = datetime.timedelta(seconds=5)


def _find_ui_object_else_click(object_to_await, object_to_click):
  """Waits for a UI object to be visible. If not, clicks another UI object.

  Args:
    object_to_await: A snippet-uiautomator selector object to be awaited.
    object_to_click: A snippet-uiautomator selector object to be clicked.
  """
  if not object_to_await.wait.exists(UI_OBJECT_WAIT_TIME_SECONDS):
    object_to_click.click()


def verify_ui_object_visible(ui_object, call_on_fail=None):
  """Verifies that a UI object is visible.

  Args:
    ui_object: A snippet-uiautomator selector object.
    call_on_fail: [Optional] Callable; method to call on failure.
  """
  ui_object_visible = ui_object.wait.exists(UI_OBJECT_WAIT_TIME_SECONDS)
  if not ui_object_visible:
    if call_on_fail is not None:
      call_on_fail()
    raise AssertionError('UI object was not visible!')


def open_jca_viewfinder(dut, log_path):
  """Sends an intent to JCA and open its viewfinder.

  Args:
    dut: An Android controller device object.
    log_path: str; log path to save screenshots.
  Raises:
    AssertionError: If JCA viewfinder is not visible.
  """
  its_device_utils.start_its_test_activity(dut.serial)
  call_on_fail = lambda: dut.take_screenshot(log_path, prefix='its_not_found')
  verify_ui_object_visible(
      dut.ui(text=ITS_ACTIVITY_TEXT),
      call_on_fail=call_on_fail
  )

  # Send intent to ItsTestActivity, which will start the correct JCA activity.
  its_device_utils.run(
      f'adb -s {dut.serial} shell am broadcast -a {ACTION_ITS_DO_JCA_CAPTURE}'
  )
  jca_capture_button_visible = dut.ui(
      res=CAPTURE_BUTTON_RESOURCE_ID).wait.exists(
          UI_OBJECT_WAIT_TIME_SECONDS)
  if not jca_capture_button_visible:
    dut.take_screenshot(log_path, prefix=VIEWFINDER_NOT_VISIBLE_PREFIX)
    logging.debug('Current UI dump: %s', dut.ui.dump())
    raise AssertionError('JCA was not started successfully!')
  dut.take_screenshot(log_path, prefix=VIEWFINDER_VISIBLE_PREFIX)


def switch_jca_camera(dut, log_path, facing):
  """Interacts with JCA UI to switch camera if necessary.

  Args:
    dut: An Android controller device object.
    log_path: str; log path to save screenshots.
    facing: str; constant describing the direction the camera lens faces.
  Raises:
    AssertionError: If JCA does not report that camera has been switched.
  """
  if facing == camera_properties_utils.LENS_FACING['BACK']:
    ui_facing_description = UI_DESCRIPTION_BACK_CAMERA
  elif facing == camera_properties_utils.LENS_FACING['FRONT']:
    ui_facing_description = UI_DESCRIPTION_FRONT_CAMERA
  else:
    raise ValueError(f'Unknown facing: {facing}')
  dut.ui(res=QUICK_SETTINGS_RESOURCE_ID).click()
  _find_ui_object_else_click(dut.ui(desc=ui_facing_description),
                             dut.ui(res=QUICK_SET_FLIP_CAMERA_RESOURCE_ID))
  if not dut.ui(desc=ui_facing_description).wait.exists(
      UI_OBJECT_WAIT_TIME_SECONDS):
    dut.take_screenshot(log_path, prefix='failed_to_switch_camera')
    logging.debug('JCA UI dump: %s', dut.ui.dump())
    raise AssertionError(f'Failed to switch to {ui_facing_description}!')
  dut.take_screenshot(
      log_path, prefix=f"switched_to_{ui_facing_description.replace(' ', '_')}"
  )
  dut.ui(res=QUICK_SETTINGS_RESOURCE_ID).click()


def default_camera_app_setup(device_id, pkg_name):
  """Setup Camera app by providing required permissions.

  Args:
    device_id: serial id of device.
    pkg_name: pkg name of the app to setup.
  Returns:
    Runtime exception from called function or None.
  """
  logging.debug('Setting up the app with permission.')
  for permission in _PERMISSIONS_LIST:
    cmd = f'pm grant {pkg_name} android.permission.{permission}'
    its_device_utils.run_adb_shell_command(device_id, cmd)
  allow_manage_storage_cmd = (
      f'appops set {pkg_name} MANAGE_EXTERNAL_STORAGE allow'
  )
  its_device_utils.run_adb_shell_command(device_id, allow_manage_storage_cmd)


def switch_default_camera(dut, facing, log_path):
  """Interacts with default camera app UI to switch camera.

  Args:
    dut: An Android controller device object.
    facing: str; constant describing the direction the camera lens faces.
    log_path: str; log path to save screenshots.
  Raises:
    AssertionError: If default camera app does not report that
      camera has been switched.
  """
  flip_camera_pattern = (
      r'(switch to|flip camera|switch camera|camera switch|switch)'
    )
  default_ui_dump = dut.ui.dump()
  logging.debug('Default camera UI dump: %s', default_ui_dump)
  root = et.fromstring(default_ui_dump)
  camera_flip_res = False
  for node in root.iter('node'):
    resource_id = node.get('resource-id')
    content_desc = node.get('content-desc')
    if re.search(
        flip_camera_pattern, content_desc, re.IGNORECASE
    ):
      logging.debug('Pattern matches')
      logging.debug('Resource id: %s', resource_id)
      logging.debug('Flip camera content-desc: %s', content_desc)
      camera_flip_res = True
      break
  if content_desc and resource_id:
    if facing == 'front' and camera_flip_res:
      if ('rear' in content_desc.lower() or 'rear' in resource_id.lower()
          or 'back' in content_desc.lower() or 'back' in resource_id.lower()
          ):
        logging.debug('Pattern found but camera is already switched.')
      else:
        dut.ui(desc=content_desc).click.wait()
    elif facing == 'rear' and camera_flip_res:
      if 'front' in content_desc.lower() or 'front' in resource_id.lower():
        logging.debug('Pattern found but camera is already switched.')
      else:
        dut.ui(desc=content_desc).click.wait()
    else:
      raise ValueError(f'Unknown facing: {facing}')

  dut.take_screenshot(
      log_path, prefix=f'switched_to_{facing}_default_camera'
  )

  if not camera_flip_res:
    raise AssertionError('Flip camera resource not found.')


def pull_img_files(device_id, input_path, output_path):
  """Pulls files from the input_path on the device to output_path.

  Args:
    device_id: serial id of device.
    input_path: File location on device.
    output_path: Location to save the file on the host.
  """
  logging.debug('Pulling files from the device')
  pull_cmd = f'adb -s {device_id} pull {input_path} {output_path}'
  its_device_utils.run(pull_cmd)


def launch_and_take_capture(dut, pkg_name, camera_facing, log_path):
  """Launches the camera app and takes still capture.

  Args:
    dut: An Android controller device object.
    pkg_name: pkg_name of the default camera app to
      be used for captures.
    camera_facing: camera lens facing orientation
    log_path: str; log path to save screenshots.

  Returns:
    img_path_on_dut: Path of the captured image on the device
  """
  device_id = dut.serial
  try:
    logging.debug('Launching app: %s', pkg_name)
    launch_cmd = f'monkey -p {pkg_name} 1'
    its_device_utils.run_adb_shell_command(device_id, launch_cmd)

    # Click OK/Done button on initial pop up windows
    if dut.ui(text=AGREE_BUTTON).wait.exists(
        timeout=WAIT_INTERVAL_FIVE_SECONDS):
      dut.ui(text=AGREE_BUTTON).click.wait()
    if dut.ui(text=OK_BUTTON_TXT).wait.exists(
        timeout=WAIT_INTERVAL_FIVE_SECONDS):
      dut.ui(text=OK_BUTTON_TXT).click.wait()
    if dut.ui(text=DONE_BUTTON_TXT).wait.exists(
        timeout=WAIT_INTERVAL_FIVE_SECONDS):
      dut.ui(text=DONE_BUTTON_TXT).click.wait()
    if dut.ui(text=CANCEL_BUTTON_TXT).wait.exists(
        timeout=WAIT_INTERVAL_FIVE_SECONDS):
      dut.ui(text=CANCEL_BUTTON_TXT).click.wait()
    switch_default_camera(dut, camera_facing, log_path)
    time.sleep(ACTIVITY_WAIT_TIME_SECONDS)
    logging.debug('Taking photo')
    its_device_utils.run_adb_shell_command(device_id, TAKE_PHOTO_CMD)
    time.sleep(ACTIVITY_WAIT_TIME_SECONDS)
    img_path_on_dut = ''
    photo_storage_path = ''
    for path in CAMERA_FILES_PATHS:
      check_path_cmd = (
          f'ls {path} && echo "Directory exists" || '
          'echo "Directory does not exist"'
      )
      cmd_output = dut.adb.shell(check_path_cmd).decode('utf-8').strip()
      if _DIR_EXISTS_TXT in cmd_output:
        photo_storage_path = path
        break
    find_file_path = (
        f'find {photo_storage_path} ! -empty -a ! -name \'.pending*\''
        ' -a -type f -name "*.jpg" -o -name "*.jpeg"'
    )
    img_path_on_dut = dut.adb.shell(find_file_path).decode('utf-8').strip()
    logging.debug('Image path on DUT: %s', img_path_on_dut)
    if JPG_FORMAT_STR not in img_path_on_dut:
      raise AssertionError('Failed to find jpg files!')
  finally:
    force_stop_app(dut, pkg_name)
  return img_path_on_dut


def force_stop_app(dut, pkg_name):
  """Force stops an app with given pkg_name.

  Args:
    dut: An Android controller device object.
    pkg_name: pkg_name of the app to be stopped.
  """
  logging.debug('Closing app: %s', pkg_name)
  force_stop_cmd = f'am force-stop {pkg_name}'
  dut.adb.shell(force_stop_cmd)


def default_camera_app_dut_setup(device_id, pkg_name):
  """Setup the device for testing default camera app.

  Args:
    device_id: serial id of device.
    pkg_name: pkg_name of the app.
  Returns:
    Runtime exception from called function or None.
  """
  default_camera_app_setup(device_id, pkg_name)
  for path in CAMERA_FILES_PATHS:
    its_device_utils.run_adb_shell_command(
        device_id, f'{REMOVE_CAMERA_FILES_CMD}{path}/*')
