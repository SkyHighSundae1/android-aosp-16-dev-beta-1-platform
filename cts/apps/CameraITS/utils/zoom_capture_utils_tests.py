# Copyright 2023 The Android Open Source Project
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
"""Tests for zoom_capture_utils."""


import unittest

import zoom_capture_utils


_CIRCLE_X = 320
_CIRCLE_Y = 240
_FOCAL_LENGTH = 1
_IMG_SIZE = (640, 480)
_OFFSET_RTOL = 0.1
_RADIUS_RTOL = 0.1


def _generate_valid_zoom_results():
  return [
      zoom_capture_utils.ZoomTestData(
          result_zoom=1,
          circle=[_CIRCLE_X, _CIRCLE_Y, 1],
          radius_tol=_RADIUS_RTOL,
          offset_tol=_OFFSET_RTOL,
          focal_length=_FOCAL_LENGTH
      ),
      zoom_capture_utils.ZoomTestData(
          result_zoom=2,
          circle=[_CIRCLE_X, _CIRCLE_Y, 2],
          radius_tol=_RADIUS_RTOL,
          offset_tol=_OFFSET_RTOL,
          focal_length=_FOCAL_LENGTH
      ),
      zoom_capture_utils.ZoomTestData(
          result_zoom=3,
          circle=[_CIRCLE_X, _CIRCLE_Y, 3],
          radius_tol=_RADIUS_RTOL,
          offset_tol=_OFFSET_RTOL,
          focal_length=_FOCAL_LENGTH
      ),
      zoom_capture_utils.ZoomTestData(
          result_zoom=4,
          circle=[_CIRCLE_X, _CIRCLE_Y, 4],
          radius_tol=_RADIUS_RTOL,
          offset_tol=_OFFSET_RTOL,
          focal_length=_FOCAL_LENGTH
      ),
  ]


class ZoomCaptureUtilsTest(unittest.TestCase):
  """Unit tests for this module."""

  def setUp(self):
    super().setUp()
    self.zoom_results = _generate_valid_zoom_results()

  def test_verify_zoom_results_enough_zoom_data(self):
    self.assertTrue(
        zoom_capture_utils.verify_zoom_results(
            self.zoom_results, _IMG_SIZE, 4, 1
        )
    )

  def test_verify_zoom_results_not_enough_zoom(self):
    self.assertFalse(
        zoom_capture_utils.verify_zoom_results(
            self.zoom_results, _IMG_SIZE, 5, 1
        )
    )

  def test_verify_zoom_results_wrong_zoom(self):
    self.zoom_results[-1].result_zoom = 5
    self.assertFalse(
        zoom_capture_utils.verify_zoom_results(
            self.zoom_results, _IMG_SIZE, 4, 1
        )
    )

  def test_verify_zoom_results_wrong_offset(self):
    self.zoom_results[-1].circle[0] = 640
    self.assertFalse(
        zoom_capture_utils.verify_zoom_results(
            self.zoom_results, _IMG_SIZE, 4, 1
        )
    )


if __name__ == '__main__':
  unittest.main()
