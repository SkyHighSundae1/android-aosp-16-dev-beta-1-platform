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
"""Utility functions for low light camera tests."""

import logging
import os.path

import cv2
import matplotlib.pyplot as plt
import numpy as np

_LOW_LIGHT_BOOST_AVG_DELTA_LUMINANCE_THRESH = 18
_LOW_LIGHT_BOOST_AVG_LUMINANCE_THRESH = 90
_BOUNDING_BOX_COLOR = (0, 255, 0)
_BOX_MIN_SIZE_RATIO = 0.08  # 8% of the cropped image width
_BOX_MAX_SIZE_RATIO = 0.5  # 50% of the cropped image width
_BOX_PADDING_RATIO = 0.2
_CROP_PADDING = 10
_EXPECTED_NUM_OF_BOXES = 20  # The captured image must result in 20 detected
                             # boxes since the test scene has 20 boxes
_KEY_BOTTOM_LEFT = 'bottom_left'
_KEY_BOTTOM_RIGHT = 'bottom_right'
_KEY_TOP_LEFT = 'top_left'
_KEY_TOP_RIGHT = 'top_right'
_MAX_ASPECT_RATIO = 1.2
_MIN_ASPECT_RATIO = 0.8
_RED_BGR_COLOR = (0, 0, 255)
_NUM_CLUSTERS = 8
_K_MEANS_ITERATIONS = 10
_K_MEANS_EPSILON = 0.5
_TEXT_COLOR = (255, 255, 255)

# Allowed tablets for low light scenes
# List entries must be entered in lowercase
TABLET_LOW_LIGHT_SCENES_ALLOWLIST = (
    'hwcmr09',  # Huawei MediaPad M5
    'gta8wifi',  # Samsung Galaxy Tab A8
    'gta8',  # Samsung Galaxy Tab A8 LTE
    'gta9pwifi',  # Samsung Galaxy Tab A9+
    'gta9p',  # Samsung Galaxy Tab A9+ 5G
    'nabu',  # Xiaomi Pad 5
    'nabu_tw',  # Xiaomi Pad 5
    'xun',  # Xiaomi Redmi Pad SE
)

# Tablet brightness mapping strings for (rear, front) facing camera tests
# List entries must be entered in lowercase
TABLET_BRIGHTNESS = {
    'hwcmr09': ('4', '8'),  # Huawei MediaPad M5
    'gta8wifi': ('6', '12'),  # Samsung Galaxy Tab A8
    'gta8': ('6', '12'),  # Samsung Galaxy Tab A8 LTE
    'gta9pwifi': ('6', '12'),  # Samsung Galaxy Tab A9+
    'gta9p': ('6', '12'),  # Samsung Galaxy Tab A9+ 5G
    'nabu': ('8', '14'),  # Xiaomi Pad 5
    'nabu_tw': ('8', '14'),  # Xiaomi Pad 5
    'xun': ('6', '12'),  # Xiaomi Redmi Pad SE
}


def _crop(img):
  """Crops the captured image according to the red square outline.

  Args:
    img: numpy array; captured image from scene_low_light.
  Returns:
    numpy array of the cropped image or the original image if the crop region
    isn't found.
  """
  # To apply k-means clustering, we need to convert the image in to an array
  # where each row represents a pixel in the image, and each column is a feature
  # In this case, the feature represents the RGB channels of the pixel
  data = img.reshape((-1, 3))
  data = np.float32(data)

  k_means_criteria = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER,
                      _K_MEANS_ITERATIONS, _K_MEANS_EPSILON)
  _, labels, centers = cv2.kmeans(data, _NUM_CLUSTERS, None, k_means_criteria,
                                  _K_MEANS_ITERATIONS,
                                  cv2.KMEANS_RANDOM_CENTERS)
  # Find the cluster closest to red
  min_dist = float('inf')
  closest_cluster_index = -1
  for index, center in enumerate(centers):
    dist = np.linalg.norm(center - np.array(_RED_BGR_COLOR))
    if dist < min_dist:
      min_dist = dist
      closest_cluster_index = index

  target_label = closest_cluster_index

  # create a mask using the data associated with the cluster closest to red
  mask = labels.flatten() == target_label
  mask = mask.reshape((img.shape[0], img.shape[1]))
  mask = mask.astype(np.uint8)

  contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL,
                                 cv2.CHAIN_APPROX_SIMPLE)

  max_area = 20
  max_box = None

  # Find the largest box that is closest to square
  for c in contours:
    x, y, w, h = cv2.boundingRect(c)
    aspect_ratio = w / h
    if _MIN_ASPECT_RATIO < aspect_ratio < _MAX_ASPECT_RATIO:
      area = w * h
      if area > max_area:
        max_area = area
        max_box = (x, y, w, h)

  # If the box is found then return the cropped image
  # otherwise the original image is returned
  if max_box:
    x, y, w, h = max_box
    cropped_img = img[
        y+_CROP_PADDING:y+h-_CROP_PADDING,
        x+_CROP_PADDING:x+w-_CROP_PADDING
    ]
    return cropped_img

  return img


def _find_boxes(image):
  """Finds boxes in the captured image for computing luminance.

  The captured image should be of scene_low_light.png. The boxes are detected
  by finding the contours by applying a threshold followed erosion.

  Args:
    image: numpy array; the captured image.
  Returns:
    array; an array of boxes, where each box is (x, y, w, h).
  """
  gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
  blur = cv2.GaussianBlur(gray, (3, 3), 0)

  thresh = cv2.adaptiveThreshold(
      blur, 255, cv2.ADAPTIVE_THRESH_MEAN_C, cv2.THRESH_BINARY, 31, -5)

  kernel = np.ones((3, 3), np.uint8)
  eroded = cv2.erode(thresh, kernel, iterations=1)

  contours, _ = cv2.findContours(eroded, cv2.RETR_EXTERNAL,
                                 cv2.CHAIN_APPROX_SIMPLE)
  boxes = []

  # Filter out boxes that are too small or too large
  # and boxes that are not square
  img_hw_size_max = max(image.shape[0], image.shape[1])
  box_min_size = int(round(img_hw_size_max * _BOX_MIN_SIZE_RATIO, 0))
  if box_min_size == 0:
    raise AssertionError('Minimum box size calculated was 0. Check cropped '
                         'image size.')
  box_max_size = int(img_hw_size_max * _BOX_MAX_SIZE_RATIO)
  for c in contours:
    x, y, w, h = cv2.boundingRect(c)
    aspect_ratio = w / h
    if (w > box_min_size and h > box_min_size and
        w < box_max_size and h < box_max_size and
        _MIN_ASPECT_RATIO < aspect_ratio < _MAX_ASPECT_RATIO):
      boxes.append((x, y, w, h))
  return boxes


def _correct_image_rotation(img, regions):
  """Corrects the captured image orientation.

  The captured image should be of scene_low_light.png. The darkest square
  must appear in the bottom right and the brightest square must appear in
  the bottom left. This is necessary in order to traverse the hilbert
  ordered squares to return a darkest to brightest ordering.

  Args:
    img: numpy array; the original image captured.
    regions: the tuple of (box, luminance) computed for each square
      in the image.
  Returns:
    numpy array; image in the corrected orientation.
  """
  corner_brightness = {
      _KEY_TOP_LEFT: regions[2][1],
      _KEY_BOTTOM_LEFT: regions[5][1],
      _KEY_TOP_RIGHT: regions[14][1],
      _KEY_BOTTOM_RIGHT: regions[17][1],
  }

  darkest_corner = ('', float('inf'))
  brightest_corner = ('', float('-inf'))

  for corner, luminance in corner_brightness.items():
    if luminance < darkest_corner[1]:
      darkest_corner = (corner, luminance)
    if luminance > brightest_corner[1]:
      brightest_corner = (corner, luminance)

  if darkest_corner == brightest_corner:
    raise AssertionError('The captured image failed to detect the location '
                         'of the darkest and brightest squares.')

  if darkest_corner[0] == _KEY_TOP_LEFT:
    if brightest_corner[0] == _KEY_BOTTOM_LEFT:
      # rotate 90 CW and then flip vertically
      img = cv2.rotate(img, cv2.ROTATE_90_CLOCKWISE)
      img = cv2.flip(img, 0)
    elif brightest_corner[0] == _KEY_TOP_RIGHT:
      # flip both vertically and horizontally
      img = cv2.flip(img, -1)
    else:
      raise AssertionError('The captured image failed to detect the location '
                           'of the brightest square.')
  elif darkest_corner[0] == _KEY_BOTTOM_LEFT:
    if brightest_corner[0] == _KEY_TOP_LEFT:
      # rotate 90 CCW
      img = cv2.rotate(img, cv2.ROTATE_90_COUNTERCLOCKWISE)
    elif brightest_corner[0] == _KEY_BOTTOM_RIGHT:
      # flip horizontally
      img = cv2.flip(img, 1)
    else:
      raise AssertionError('The captured image failed to detect the location '
                           'of the brightest square.')
  elif darkest_corner[0] == _KEY_TOP_RIGHT:
    if brightest_corner[0] == _KEY_TOP_LEFT:
      # flip vertically
      img = cv2.flip(img, 0)
    elif brightest_corner[0] == _KEY_BOTTOM_RIGHT:
      # rotate 90 CW
      img = cv2.rotate(img, cv2.ROTATE_90_CLOCKWISE)
    else:
      raise AssertionError('The captured image failed to detect the location '
                           'of the brightest square.')
  elif darkest_corner[0] == _KEY_BOTTOM_RIGHT:
    if brightest_corner[0] == _KEY_BOTTOM_LEFT:
      # correct orientation
      pass
    elif brightest_corner[0] == _KEY_TOP_RIGHT:
      # rotate 90 and flip horizontally
      img = cv2.rotate(img, cv2.ROTATE_90_CLOCKWISE)
      img = cv2.flip(img, 1)
    else:
      raise AssertionError('The captured image failed to detect the location '
                           'of the brightest square.')
  return img


def _compute_luminance_regions(image, boxes):
  """Compute the luminance for each box in scene_low_light.

  Args:
    image: numpy array; captured image.
    boxes: array; array of boxes where each box is (x, y, w, h).
  Returns:
    Array of tuples where each tuple is (box, luminance).
  """
  intensities = []
  for b in boxes:
    x, y, w, h = b
    padding = min(w, h) * _BOX_PADDING_RATIO
    left = int(x + padding)
    top = int(y + padding)
    right = int(x + w - padding)
    bottom = int(y + h - padding)
    box = image[top:bottom, left:right]
    box_xyz = cv2.cvtColor(box, cv2.COLOR_BGR2XYZ)
    intensity = int(np.mean(box_xyz[1]))
    intensities.append((b, intensity))
  return intensities


def _draw_luminance(image, intensities):
  """Draws the luminance for each box in scene_low_light. Useful for debugging.

  Args:
    image: numpy array; captured image.
    intensities: array; array of tuples (box, luminance intensity).
  """
  for (b, intensity) in intensities:
    x, y, w, h = b
    padding = min(w, h) * _BOX_PADDING_RATIO
    left = int(x + padding)
    top = int(y + padding)
    right = int(x + w - padding)
    bottom = int(y + h - padding)
    cv2.rectangle(image, (left, top), (right, bottom), _BOUNDING_BOX_COLOR, 2)
    cv2.putText(image, f'{intensity}', (x, y - 10),
                cv2.FONT_HERSHEY_PLAIN, 1, _TEXT_COLOR, 1, 2)


def _compute_avg(results):
  """Computes the average luminance of the first 6 boxes.

  The boxes are part of scene_low_light.

  Args:
    results: A list of tuples where each tuple is (box, luminance).
  Returns:
    float; The average luminance of the first 6 boxes.
  """
  luminance_values = [luminance for _, luminance in results[:6]]
  avg = sum(luminance_values) / len(luminance_values)
  return avg


def _compute_avg_delta_of_successive_boxes(results):
  """Computes the delta of successive boxes & takes the average of the first 5.

  The boxes are part of scene_low_light.

  Args:
    results: A list of tuples where each tuple is (box, luminance).
  Returns:
    float; The average of the first 5 deltas of successive boxes.
  """
  luminance_values = [luminance for _, luminance in results[:6]]
  delta = [luminance_values[i] - luminance_values[i - 1]
           for i in range(1, len(luminance_values))]
  avg = sum(delta) / len(delta)
  return avg


def _plot_results(results, file_stem):
  """Plots the computed luminance for each box in scene_low_light.

  Args:
    results: A list of tuples where each tuple is (box, luminance).
    file_stem: The output file where the plot is saved.
  """
  luminance_values = [luminance for _, luminance in results]
  box_labels = [f'Box {i + 1}' for i in range(len(results))]

  plt.figure(figsize=(10, 6))
  plt.plot(box_labels, luminance_values, marker='o', linestyle='-', color='b')
  plt.scatter(box_labels, luminance_values, color='r')

  plt.title('Luminance for each Box')
  plt.xlabel('Boxes')
  plt.ylabel('Luminance (pixel intensity)')
  plt.grid('True')
  plt.xticks(rotation=45)
  plt.savefig(f'{file_stem}_luminance_plot.png', dpi=300)
  plt.close()


def _plot_successive_difference(results, file_stem):
  """Plots the successive difference in luminance between each box.

  The boxes are part of scene_low_light.

  Args:
    results: A list of tuples where each tuple is (box, luminance).
    file_stem: The output file where the plot is saved.
  """
  luminance_values = [luminance for _, luminance in results]
  delta = [luminance_values[i] - luminance_values[i - 1]
           for i in range(1, len(luminance_values))]
  box_labels = [f'Box {i} to Box {i + 1}' for i in range(1, len(results))]

  plt.figure(figsize=(10, 6))
  plt.plot(box_labels, delta, marker='o', linestyle='-', color='b')
  plt.scatter(box_labels, delta, color='r')

  plt.title('Difference in Luminance Between Successive Boxes')
  plt.xlabel('Box Transition')
  plt.ylabel('Luminance Difference')
  plt.grid('True')
  plt.xticks(rotation=45)
  file = f'{file_stem}_luminance_difference_between_successive_boxes_plot.png'
  plt.savefig(file, dpi=300)
  plt.close()


def _sort_by_columns(regions):
  """Sort the regions by columns and then by row within each column.

  These regions are part of scene_low_light.

  Args:
    regions: The tuple of (box, luminance) of each square.
  Returns:
    array; an array of tuples of (box, luminance) sorted by columns then by row
      within each column.
  """
  # The input is 20 elements. The first two and last two elements represent the
  # 4 boxes on the outside used for diagnostics. Boxes in indices 2 through 17
  # represent the elements in the 4x4 grid.

  # Sort all elements by column
  col_sorted = sorted(regions, key=lambda r: r[0][0])

  # Sort elements within each column by row
  result = []
  result.extend(sorted(col_sorted[:2], key=lambda r: r[0][1]))

  for i in range(4):
    # take 4 rows per column and then sort the rows
    # skip the first two elements
    offset = i*4+2
    col = col_sorted[offset:(offset+4)]
    result.extend(sorted(col, key=lambda r: r[0][1]))

  result.extend(sorted(col_sorted[-2:], key=lambda r: r[0][1]))
  return result


def analyze_low_light_scene_capture(
    file_stem,
    img,
    avg_luminance_threshold=_LOW_LIGHT_BOOST_AVG_LUMINANCE_THRESH,
    avg_delta_luminance_threshold=_LOW_LIGHT_BOOST_AVG_DELTA_LUMINANCE_THRESH):
  """Analyze a captured frame to check if it meets low light scene criteria.

  The capture is cropped first, then detects for boxes, and then computes the
  luminance of each box.

  Args:
    file_stem: The file prefix for results saved.
    img: numpy array; The captured image loaded by cv2 as and available for
      analysis.
    avg_luminance_threshold: minimum average luminance of the first 6 boxes.
    avg_delta_luminance_threshold: minimum average difference in luminance
      of the first 5 successive boxes of luminance.
  """
  cv2.imwrite(f'{file_stem}_original.jpg', img)
  img = _crop(img)
  cv2.imwrite(f'{file_stem}_cropped.jpg', img)
  boxes = _find_boxes(img)
  if len(boxes) != _EXPECTED_NUM_OF_BOXES:
    raise AssertionError('The captured image failed to detect the expected '
                         'number of boxes. '
                         'Check the captured image to see if the image was '
                         'correctly captured and try again. '
                         f'Actual: {len(boxes)}, '
                         f'Expected: {_EXPECTED_NUM_OF_BOXES}')

  regions = _compute_luminance_regions(img, boxes)

  # Sorted so each column is read left to right
  sorted_regions = _sort_by_columns(regions)
  img = _correct_image_rotation(img, sorted_regions)
  cv2.imwrite(f'{file_stem}_rotated.jpg', img)

  # The orientation of the image may have changed which will affect the
  # coordinates of the squares. Therefore, locate the squares, recompute the
  # regions, and sort again
  boxes = _find_boxes(img)
  regions = _compute_luminance_regions(img, boxes)
  sorted_regions = _sort_by_columns(regions)

  _draw_luminance(img, regions)
  cv2.imwrite(f'{file_stem}_result.jpg', img)

  # Reorder this so the regions are increasing in luminance according to the
  # Hilbert curve arrangement pattern of the grid
  # See scene_low_light_reference.png which indicates the order of each
  # box
  hilbert_ordered = [
      sorted_regions[17],
      sorted_regions[13],
      sorted_regions[12],
      sorted_regions[16],
      sorted_regions[15],
      sorted_regions[14],
      sorted_regions[10],
      sorted_regions[11],
      sorted_regions[7],
      sorted_regions[6],
      sorted_regions[2],
      sorted_regions[3],
      sorted_regions[4],
      sorted_regions[8],
      sorted_regions[9],
      sorted_regions[5],
  ]
  _plot_results(hilbert_ordered, file_stem)
  _plot_successive_difference(hilbert_ordered, file_stem)
  avg = _compute_avg(hilbert_ordered)
  delta_avg = _compute_avg_delta_of_successive_boxes(hilbert_ordered)
  test_name = os.path.basename(file_stem)

  # the following print statements are necessary for telemetry
  # do not convert to logging.debug
  print(f'{test_name}_avg_luma: {avg:.2f}')
  print(f'{test_name}_delta_avg_luma: {delta_avg:.2f}')
  chart_luma_values = [v[1] for v in hilbert_ordered]
  print(f'{test_name}_chart_luma: {chart_luma_values}')

  logging.debug('average luminance of the 6 boxes: %.2f', avg)
  logging.debug('average difference in luminance of 5 successive boxes: %.2f',
                delta_avg)
  if avg < avg_luminance_threshold:
    raise AssertionError('Average luminance of the first 6 boxes did not '
                         'meet minimum requirements for low light scene '
                         'criteria. '
                         f'Actual: {avg:.2f}, '
                         f'Expected: {avg_luminance_threshold}')
  if delta_avg < avg_delta_luminance_threshold:
    raise AssertionError('The average difference in luminance of the first 5 '
                         'successive boxes did not meet minimum requirements '
                         'for low light scene criteria. '
                         f'Actual: {delta_avg:.2f}, '
                         f'Expected: {avg_delta_luminance_threshold}')
