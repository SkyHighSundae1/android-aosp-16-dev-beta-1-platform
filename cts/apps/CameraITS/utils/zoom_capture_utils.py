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
"""Utility functions for zoom capture.
"""

from collections.abc import Iterable
import dataclasses
import logging
import math
import cv2
import matplotlib.pyplot as plt
import numpy

import camera_properties_utils
import capture_request_utils
import image_processing_utils
import opencv_processing_utils

_CIRCLE_COLOR = 0  # [0: black, 255: white]
_CIRCLE_AR_RTOL = 0.15  # contour width vs height (aspect ratio)
_CIRCLISH_RTOL = 0.05  # contour area vs ideal circle area pi*((w+h)/4)**2
_CONTOUR_AREA_LOGGING_THRESH = 0.8  # logging tol to cut down spam in log file
_CV2_LINE_THICKNESS = 3  # line thickness for drawing on images
_CV2_RED = (255, 0, 0)  # color in cv2 to draw lines
_DEFAULT_FOV_RATIO = 1  # ratio of sub camera's fov over logical camera's fov
_MIN_AREA_RATIO = 0.00013  # Found empirically with partners
_MIN_CIRCLE_PTS = 25
_MIN_FOCUS_DIST_TOL = 0.80  # allow charts a little closer than min
_OFFSET_ATOL = 10  # number of pixels
_OFFSET_RTOL_MIN_FD = 0.30
_RADIUS_RTOL_MIN_FD = 0.15
OFFSET_RTOL = 1.0  # TODO: b/342176245 - enable offset check w/ marker identity
RADIUS_RTOL = 0.10
ZOOM_MIN_THRESH = 2.0
ZOOM_MAX_THRESH = 10.0
ZOOM_RTOL = 0.01  # variation of zoom ratio due to floating point
PRV_Z_RTOL = 0.02  # 2% variation of zoom ratio between request and result
PREFERRED_BASE_ZOOM_RATIO = 1  # Preferred base image for zoom data verification
JPEG_STR = 'jpg'


@dataclasses.dataclass
class ZoomTestData:
  """Class to store zoom-related metadata for a capture."""
  result_zoom: float
  circle: Iterable[float]  # TODO: b/338638040 - create a dataclass for circles
  radius_tol: float
  offset_tol: float
  focal_length: float
  physical_id: int = dataclasses.field(default=None)


def get_test_tols_and_cap_size(cam, props, chart_distance, debug):
  """Determine the tolerance per camera based on test rig and camera params.

  Cameras are pre-filtered to only include supportable cameras.
  Supportable cameras are: YUV(RGB)

  Args:
    cam: camera object
    props: dict; physical camera properties dictionary
    chart_distance: float; distance to chart in cm
    debug: boolean; log additional data

  Returns:
    dict of TOLs with camera focal length as key
    largest common size across all cameras
  """
  ids = camera_properties_utils.logical_multi_camera_physical_ids(props)
  physical_props = {}
  physical_ids = []
  for i in ids:
    physical_props[i] = cam.get_camera_properties_by_id(i)
    # find YUV capable physical cameras
    if camera_properties_utils.backward_compatible(physical_props[i]):
      physical_ids.append(i)

  # find physical camera focal lengths that work well with rig
  chart_distance_m = abs(chart_distance)/100  # convert CM to M
  test_tols = {}
  test_yuv_sizes = []
  for i in physical_ids:
    yuv_sizes = capture_request_utils.get_available_output_sizes(
        'yuv', physical_props[i])
    test_yuv_sizes.append(yuv_sizes)
    if debug:
      logging.debug('cam[%s] yuv sizes: %s', i, str(yuv_sizes))

    # determine if minimum focus distance is less than rig depth
    min_fd = physical_props[i]['android.lens.info.minimumFocusDistance']
    for fl in physical_props[i]['android.lens.info.availableFocalLengths']:
      logging.debug('cam[%s] min_fd: %.3f (diopters), fl: %.2f', i, min_fd, fl)
      if (math.isclose(min_fd, 0.0, rel_tol=1E-6) or  # fixed focus
          (1.0/min_fd < chart_distance_m*_MIN_FOCUS_DIST_TOL)):
        test_tols[fl] = (RADIUS_RTOL, OFFSET_RTOL)
      else:
        test_tols[fl] = (_RADIUS_RTOL_MIN_FD, _OFFSET_RTOL_MIN_FD)
        logging.debug('loosening RTOL for cam[%s]: '
                      'min focus distance too large.', i)
  # find intersection of formats for max common format
  common_sizes = list(set.intersection(*[set(list) for list in test_yuv_sizes]))
  if debug:
    logging.debug('common_fmt: %s', max(common_sizes))

  return test_tols, max(common_sizes)


def find_center_circle(
    img, img_name, size, zoom_ratio, min_zoom_ratio,
    expected_color=_CIRCLE_COLOR, circle_ar_rtol=_CIRCLE_AR_RTOL,
    circlish_rtol=_CIRCLISH_RTOL, min_circle_pts=_MIN_CIRCLE_PTS,
    fov_ratio=_DEFAULT_FOV_RATIO, debug=False, draw_color=_CV2_RED,
    write_img=True):
  """Find circle closest to image center for scene with multiple circles.

  Finds all contours in the image. Rejects those too small and not enough
  points to qualify as a circle. The remaining contours must have center
  point of color=color and are sorted based on distance from the center
  of the image. The contour closest to the center of the image is returned.
  If circle is not found due to zoom ratio being larger than ZOOM_MAX_THRESH
  or the circle being cropped, None is returned.

  Note: hierarchy is not used as the hierarchy for black circles changes
  as the zoom level changes.

  Args:
    img: numpy img array with pixel values in [0,255]
    img_name: str file name for saved image
    size: [width, height] of the image
    zoom_ratio: zoom_ratio for the particular capture
    min_zoom_ratio: min_zoom_ratio supported by the camera device
    expected_color: int 0 --> black, 255 --> white
    circle_ar_rtol: float aspect ratio relative tolerance
    circlish_rtol: float contour area vs ideal circle area pi*((w+h)/4)**2
    min_circle_pts: int minimum number of points to define a circle
    fov_ratio: ratio of sub camera over logical camera's field of view
    debug: bool to save extra data
    draw_color: cv2 color in RGB to draw circle and circle center on the image
    write_img: bool: True - save image with circle and center
                     False - don't save image.

  Returns:
    circle: [center_x, center_y, radius]
  """

  width, height = size
  min_area = (
      _MIN_AREA_RATIO * width * height * zoom_ratio * zoom_ratio * fov_ratio)

  # create a copy of image to avoid modification on the original image since
  # image_processing_utils.convert_image_to_uint8 uses mutable np array methods
  if debug:
    img = numpy.ndarray.copy(img)

  # convert [0, 1] image to [0, 255] and cast as uint8
  if img.dtype != numpy.uint8:
    img = image_processing_utils.convert_image_to_uint8(img)

  # gray scale & otsu threshold to binarize the image
  gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
  _, img_bw = cv2.threshold(
      numpy.uint8(gray), 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)

  # use OpenCV to find contours (connected components)
  contours = opencv_processing_utils.find_all_contours(255-img_bw)

  # write copy of image for debug purposes
  if debug:
    img_copy_name = img_name.split('.')[0] + '_copy.jpg'
    image_processing_utils.write_image(numpy.expand_dims(
        (255-img_bw).astype(numpy.float)/255.0, axis=2), img_copy_name)

  # check contours and find the best circle candidates
  circles = []
  img_ctr = [gray.shape[1] // 2, gray.shape[0] // 2]
  logging.debug('img center x,y: %d, %d', img_ctr[0], img_ctr[1])
  logging.debug('min area: %d, min circle pts: %d', min_area, min_circle_pts)
  logging.debug('circlish_rtol: %.3f', circlish_rtol)

  for contour in contours:
    area = cv2.contourArea(contour)
    if area > min_area * _CONTOUR_AREA_LOGGING_THRESH:  # skip tiny contours
      logging.debug('area: %d, min_area: %d, num_pts: %d, min_circle_pts: %d',
                    area, min_area, len(contour), min_circle_pts)
    if area > min_area and len(contour) >= min_circle_pts:
      shape = opencv_processing_utils.component_shape(contour)
      radius = (shape['width'] + shape['height']) / 4
      circle_color = img_bw[shape['cty']][shape['ctx']]
      circlish = round((math.pi * radius**2) / area, 4)
      logging.debug('color: %s, circlish: %.2f, WxH: %dx%d',
                    circle_color, circlish, shape['width'], shape['height'])
      if (circle_color == expected_color and
          math.isclose(1, circlish, rel_tol=circlish_rtol) and
          math.isclose(shape['width'], shape['height'],
                       rel_tol=circle_ar_rtol)):
        logging.debug('circle found: r: %.2f, area: %.2f\n', radius, area)
        circles.append([shape['ctx'], shape['cty'], radius, circlish, area])
      else:
        logging.debug('circle rejected: bad color, circlish or aspect ratio\n')

  if not circles:
    zoom_ratio_value = zoom_ratio / min_zoom_ratio
    if zoom_ratio_value >= ZOOM_MAX_THRESH:
      logging.debug('No circle was detected, but zoom %.2f exceeds'
                    ' maximum zoom threshold', zoom_ratio_value)
      return None
    else:
      raise AssertionError(
          'No circle detected for zoom ratio <= '
          f'{ZOOM_MAX_THRESH}. '
          'Take pictures according to instructions carefully!')
  else:
    logging.debug('num of circles found: %s', len(circles))

  if debug:
    logging.debug('circles [x, y, r, pi*r**2/area, area]: %s', str(circles))

  # find circle closest to center
  circle = min(
      circles, key=lambda x: math.hypot(x[0] - img_ctr[0], x[1] - img_ctr[1]))

  # check if circle is cropped because of zoom factor
  if opencv_processing_utils.is_circle_cropped(circle, size):
    logging.debug('zoom %.2f is too large! Skip further captures', zoom_ratio)
    return None

  # mark image center
  size = gray.shape
  m_x, m_y = size[1] // 2, size[0] // 2
  marker_size = _CV2_LINE_THICKNESS * 10
  cv2.drawMarker(img, (m_x, m_y), draw_color, markerType=cv2.MARKER_CROSS,
                 markerSize=marker_size, thickness=_CV2_LINE_THICKNESS)

  # add circle to saved image
  center_i = (int(round(circle[0], 0)), int(round(circle[1], 0)))
  radius_i = int(round(circle[2], 0))
  cv2.circle(img, center_i, radius_i, draw_color, _CV2_LINE_THICKNESS)
  if write_img:
    image_processing_utils.write_image(img / 255.0, img_name)

  return circle


def preview_zoom_data_to_string(test_data):
  """Returns formatted string from test_data.

  Floats are capped at 2 floating points.

  Args:
    test_data: ZoomTestData with relevant test data.

  Returns:
    Formatted String
  """
  output = []
  for key, value in dataclasses.asdict(test_data).items():
    if isinstance(value, float):
      output.append(f'{key}: {value:.2f}')
    elif isinstance(value, list):
      output.append(
          f"{key}: [{', '.join([f'{item:.2f}' for item in value])}]")
    else:
      output.append(f'{key}: {value}')

  return ', '.join(output)


def verify_zoom_results(test_data, size, z_max, z_min):
  """Verify that the output images' zoom level reflects the correct zoom ratios.

  This test verifies that the center and radius of the circles in the output
  images reflects the zoom ratios being set. The larger the zoom ratio, the
  larger the circle. And the distance from the center of the circle to the
  center of the image is proportional to the zoom ratio as well.

  Args:
    test_data: Iterable[ZoomTestData]
    size: array; the width and height of the images
    z_max: float; the maximum zoom ratio being tested
    z_min: float; the minimum zoom ratio being tested

  Returns:
    Boolean whether the test passes (True) or not (False)
  """
  # assert some range is tested before circles get too big
  test_success = True

  zoom_max_thresh = ZOOM_MAX_THRESH
  z_max_ratio = z_max / z_min
  if z_max_ratio < ZOOM_MAX_THRESH:
    zoom_max_thresh = z_max_ratio

  # handle capture orders like [1, 0.5, 1.5, 2...]
  test_data_zoom_values = [v.result_zoom for v in test_data]
  test_data_max_z = max(test_data_zoom_values) / min(test_data_zoom_values)
  logging.debug('test zoom ratio max: %.2f vs threshold %.2f',
                test_data_max_z, zoom_max_thresh)

  if not math.isclose(test_data_max_z, zoom_max_thresh, rel_tol=ZOOM_RTOL):
    test_success = False
    e_msg = (f'Max zoom ratio tested: {test_data_max_z:.4f}, '
             f'range advertised min: {z_min}, max: {z_max} '
             f'THRESH: {zoom_max_thresh + ZOOM_RTOL}')
    logging.error(e_msg)

  return test_success and verify_zoom_data(test_data, size)


def verify_zoom_data(test_data, size, plot_name_stem=None):
  """Verify that the output images' zoom level reflects the correct zoom ratios.

  This test verifies that the center and radius of the circles in the output
  images reflects the zoom ratios being set. The larger the zoom ratio, the
  larger the circle. And the distance from the center of the circle to the
  center of the image is proportional to the zoom ratio as well.

  Args:
    test_data: Iterable[ZoomTestData]
    size: array; the width and height of the images
    plot_name_stem: str; log path and name of the plot

  Returns:
    Boolean whether the test passes (True) or not (False)
  """
  # assert some range is tested before circles get too big
  test_success = True

  # initialize relative size w/ zoom[0] for diff zoom ratio checks
  radius_0 = float(test_data[0].circle[2])
  z_0 = float(test_data[0].result_zoom)

  # use 1x ~ 1.1x data as base image if available
  if z_0 < PREFERRED_BASE_ZOOM_RATIO:
    for i, data in enumerate(test_data):
      if (test_data[i].result_zoom >= PREFERRED_BASE_ZOOM_RATIO and
          math.isclose(test_data[i].result_zoom, PREFERRED_BASE_ZOOM_RATIO,
                       rel_tol=0.1)):
        radius_0 = float(test_data[i].circle[2])
        z_0 = float(test_data[i].result_zoom)
        break
  logging.debug('z_0: %.3f, radius_0: %.3f', z_0, radius_0)
  if plot_name_stem:
    frame_numbers = []
    z_variations = []
    rel_variations = []
    radius_tols = []
    max_rel_variation = None
    max_rel_variation_zoom = None
  for i, data in enumerate(test_data):
    logging.debug(' ')  # add blank line between frames
    logging.debug('Frame# %d {%s}', i, preview_zoom_data_to_string(data))
    logging.debug('Zoom: %.2f, fl: %.2f', data.result_zoom, data.focal_length)
    offset_xy = [(data.circle[0] - size[0] // 2),
                 (data.circle[1] - size[1] // 2)]
    logging.debug('Circle r: %.1f, center offset x, y: %d, %d',
                  data.circle[2], offset_xy[0], offset_xy[1])
    z_ratio = data.result_zoom / z_0

    # check relative size against zoom[0]
    radius_ratio = data.circle[2] / radius_0

    # Calculate variations
    z_variation = z_ratio - radius_ratio
    relative_variation = abs(z_variation) / max(abs(z_ratio), abs(radius_ratio))

    # Store values for plotting
    if plot_name_stem:
      frame_numbers.append(i)
      z_variations.append(z_variation)
      rel_variations.append(relative_variation)
      radius_tols.append(data.radius_tol)
      if max_rel_variation is None or relative_variation > max_rel_variation:
        max_rel_variation = relative_variation
        max_rel_variation_zoom = data.result_zoom

    logging.debug('r ratio req: %.3f, measured: %.3f',
                  z_ratio, radius_ratio)
    msg = (f'{i} Circle radius ratio: result({data.result_zoom:.3f}/{z_0:.3f}):'
           f' {z_ratio:.3f}, circle({data.circle[2]:.3f}/{radius_0:.3f}):'
           f' {radius_ratio:.3f}, RTOL: {data.radius_tol}'
           f' z_var: {z_variation:.3f}, rel_var: {relative_variation:.3f}')
    if not math.isclose(z_ratio, radius_ratio, rel_tol=data.radius_tol):
      test_success = False
      logging.error(msg)
    else:
      logging.debug(msg)

    # check relative offset against init vals w/ no focal length change
    # set init values for first capture or change in physical cam focal length
    if i == 0 or test_data[i-1].focal_length != data.focal_length:
      z_init = float(data.result_zoom)
      offset_hypot_init = math.hypot(offset_xy[0], offset_xy[1])
      logging.debug('offset_hypot_init: %.3f', offset_hypot_init)
      d_msg = (f'-- init {i} zoom: {data.result_zoom:.2f}, '
               f'offset init: {offset_hypot_init:.1f}, '
               f'offset rel: {math.hypot(offset_xy[0], offset_xy[1]):.1f}, '
               f'zoom: {z_ratio:.1f} ')
      logging.debug(d_msg)
    else:  # check
      z_ratio = data.result_zoom / z_init
      offset_hypot_rel = math.hypot(offset_xy[0], offset_xy[1]) / z_ratio
      logging.debug('offset_hypot_rel: %.3f', offset_hypot_rel)

      rel_tol = data.offset_tol
      if not math.isclose(offset_hypot_init, offset_hypot_rel,
                          rel_tol=rel_tol, abs_tol=_OFFSET_ATOL):
        test_success = False
        e_msg = (f'{i} zoom: {data.result_zoom:.2f}, '
                 f'offset init: {offset_hypot_init:.4f}, '
                 f'offset rel: {offset_hypot_rel:.4f}, '
                 f'Zoom: {z_ratio:.1f}, '
                 f'RTOL: {rel_tol}, ATOL: {_OFFSET_ATOL}')
        logging.error(e_msg)
      else:
        d_msg = (f'{i} zoom: {data.result_zoom:.2f}, '
                 f'offset init: {offset_hypot_init:.1f}, '
                 f'offset rel: {offset_hypot_rel:.1f}, '
                 f'offset dist: {math.hypot(offset_xy[0], offset_xy[1]):.1f}, '
                 f'Zoom: {z_ratio:.1f}, '
                 f'RTOL: {rel_tol}, ATOL: {_OFFSET_ATOL}')
        logging.debug(d_msg)

  if plot_name_stem:
    plot_name = plot_name_stem.split('/')[-1].split('.')[0]
    # Don't change print to logging. Used for KPI.
    print(f'{plot_name}_max_rel_variation: ', max_rel_variation)
    print(f'{plot_name}_max_rel_variation_zoom: ', max_rel_variation_zoom)

    # Calculate RMS values
    rms_z_variations = numpy.sqrt(numpy.mean(numpy.square(z_variations)))
    rms_rel_variations = numpy.sqrt(numpy.mean(numpy.square(rel_variations)))

    # Print RMS values
    print(f'{plot_name}_rms_z_variations: ', rms_z_variations)
    print(f'{plot_name}_rms_rel_variations: ', rms_rel_variations)

    plot_variation(frame_numbers, z_variations, None,
                   f'{plot_name_stem}_variations.png', 'Zoom Variation')
    plot_variation(frame_numbers, rel_variations, radius_tols,
                   f'{plot_name_stem}_relative.png', 'Relative Variation')

  return test_success


def verify_preview_zoom_results(test_data, size, z_max, z_min, z_step_size,
                                plot_name_stem):
  """Verify that the output images' zoom level reflects the correct zoom ratios.

  This test verifies that the center and radius of the circles in the output
  images reflects the zoom ratios being set. The larger the zoom ratio, the
  larger the circle. And the distance from the center of the circle to the
  center of the image is proportional to the zoom ratio as well. Verifies
  that circles are detected throughout the zoom range.

  Args:
    test_data: Iterable[ZoomTestData]
    size: array; the width and height of the images
    z_max: float; the maximum zoom ratio being tested
    z_min: float; the minimum zoom ratio being tested
    z_step_size: float; zoom step size to zoom from z_min to z_max
    plot_name_stem: str; log path and name of the plot

  Returns:
    Boolean whether the test passes (True) or not (False)
  """
  test_success = True

  test_data_zoom_values = [v.result_zoom for v in test_data]
  results_z_max = max(test_data_zoom_values)
  results_z_min = min(test_data_zoom_values)
  logging.debug('capture result: min zoom: %.2f vs max zoom: %.2f',
                results_z_min, results_z_max)

  # check if max zoom in capture result close to requested zoom range
  if (math.isclose(results_z_max, z_max, rel_tol=PRV_Z_RTOL) or
      math.isclose(results_z_max, z_max - z_step_size, rel_tol=PRV_Z_RTOL)):
    d_msg = (f'results_z_max = {results_z_max:.2f} is close to requested '
             f'z_max = {z_max:.2f} or z_max-step = {z_max-z_step_size:.2f} '
             f'by {PRV_Z_RTOL:.2f} Tol')
    logging.debug(d_msg)
  else:
    test_success = False
    e_msg = (f'Max zoom ratio {results_z_max:.4f} in capture results '
             f'not close to {z_max:.2f} and '
             f'z_max-step = {z_max-z_step_size:.2f} by {PRV_Z_RTOL:.2f} '
             f'tolerance.')
    logging.error(e_msg)

  if math.isclose(results_z_min, z_min, rel_tol=PRV_Z_RTOL):
    d_msg = (f'results_z_min = {results_z_min:.2f} is close to requested '
             f'z_min = {z_min:.2f} by {PRV_Z_RTOL:.2f} Tol')
    logging.debug(d_msg)
  else:
    test_success = False
    e_msg = (f'Min zoom ratio {results_z_min:.4f} in capture results '
             f'not close to {z_min:.2f} by {PRV_Z_RTOL:.2f} tolerance.')
    logging.error(e_msg)

  return test_success and verify_zoom_data(test_data, size, plot_name_stem)


def get_preview_zoom_params(zoom_range, steps):
  """Returns zoom min, max, step_size based on zoom range and steps.

  Determine zoom min, max, step_size based on zoom range, steps.
  Zoom max is capped due to current ITS box size limitation.

  Args:
    zoom_range: [float,float]; Camera's zoom range
    steps: int; number of steps

  Returns:
    zoom_min: minimum zoom
    zoom_max: maximum zoom
    zoom_step_size: size of zoom steps
  """
  # Determine test zoom range
  logging.debug('z_range = %s', str(zoom_range))
  zoom_min, zoom_max = float(zoom_range[0]), float(zoom_range[1])
  zoom_max = min(zoom_max, ZOOM_MAX_THRESH * zoom_min)

  zoom_step_size = (zoom_max-zoom_min) / (steps-1)
  logging.debug('zoomRatioRange = %s z_min = %f z_max = %f z_stepSize = %f',
                str(zoom_range), zoom_min, zoom_max, zoom_step_size)

  return zoom_min, zoom_max, zoom_step_size


def plot_variation(frame_numbers, variations, tolerances, plot_name, ylabel):
  """Plots a variation against frame numbers with corresponding tolerances.

  Args:
    frame_numbers: List of frame numbers.
    variations: List of variations.
    tolerances: List of tolerances corresponding to each variation.
    plot_name: Name for the plot file.
    ylabel: Label for the y-axis.
  """

  plt.figure(figsize=(40, 10))

  plt.scatter(frame_numbers, variations, marker='o', linestyle='-',
              color='blue', label=ylabel)

  if tolerances:
    plt.plot(frame_numbers, tolerances, linestyle='--', color='red',
             label='Tolerance')

  plt.xlabel('Frame Number', fontsize=12)
  plt.ylabel(ylabel, fontsize=12)
  plt.title(f'{ylabel} vs. Frame Number', fontsize=14)

  plt.legend()

  plt.grid(axis='y', linestyle='--')
  plt.savefig(plot_name)
  plt.close()

