# Copyright 2022 The Android Open Source Project
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
"""Utility functions for processing video recordings.
"""
# Each item in this list corresponds to quality levels defined per
# CamcorderProfile. For Video ITS, we will currently test below qualities
# only if supported by the camera device.


import logging
import math
import os.path
import re
import subprocess
import error_util
import image_processing_utils


COLORSPACE_HDR = 'bt2020'
HR_TO_SEC = 3600
INDEX_FIRST_SUBGROUP = 1
MIN_TO_SEC = 60

ITS_SUPPORTED_QUALITIES = (
    'HIGH',
    '2160P',
    '1080P',
    '720P',
    '480P',
    'CIF',
    'QCIF',
    'QVGA',
    'LOW',
    'VGA'
)

LOW_RESOLUTION_SIZES = (
    '176x144',
    '192x144',
    '352x288',
    '384x288',
    '320x240',
)

LOWEST_RES_TESTED_AREA = 640*360

VIDEO_QUALITY_SIZE = {
    # '480P', '1080P', HIGH' & 'LOW' are not included as they are DUT-dependent
    '2160P': '3840x2160',
    '720P': '1280x720',
    'VGA': '640x480',
    'CIF': '352x288',
    'QVGA': '320x240',
    'QCIF': '176x144',
}


def clamp_preview_sizes(preview_sizes, min_area=0, max_area=math.inf):
  """Returns a list of preview_sizes with areas between min/max_area.

  Args:
    preview_sizes: list; sizes to be filtered (ex. "1280x720")
    min_area: int; optional filter to eliminate sizes <= to the specified
        area (ex. 640*480).
    max_area: int; optional filter to eliminate sizes >= to the specified
        area (ex. 3840*2160).
  Returns:
    preview_sizes: list; filtered preview sizes clamped by min/max_area.
  """
  size_to_area = lambda size: int(size.split('x')[0])*int(size.split('x')[1])
  filtered_preview_sizes = [
      size for size in preview_sizes
      if max_area >= size_to_area(size) >= min_area]
  if not filtered_preview_sizes:
    raise AssertionError('No preview size within the specified area range!')
  logging.debug(
      'No preview sizes between %s and %s', min_area, max_area)
  return filtered_preview_sizes


def get_largest_common_preview_video_size(cam, camera_id):
  """Returns the largest, common size between preview and video.

  Args:
    cam: camera object.
    camera_id: str; camera ID.

  Returns:
    largest_common_size: str; largest common size between preview & video.
  """
  supported_preview_sizes = cam.get_all_supported_preview_sizes(camera_id)
  supported_video_qualities = cam.get_supported_video_qualities(camera_id)
  logging.debug('Supported video profiles & IDs: %s', supported_video_qualities)

  # Make a list of supported_video_sizes from video qualities
  supported_video_sizes = []
  for quality in supported_video_qualities:
    video_quality = quality.split(':')[0]  # form is ['CIF:3', '480P:4', ...]
    if video_quality in VIDEO_QUALITY_SIZE:
      supported_video_sizes.append(VIDEO_QUALITY_SIZE[video_quality])
  logging.debug(
      'Supported video sizes: %s', supported_video_sizes)

  # Use areas of video sizes to find the largest common size
  size_to_area = lambda s: int(s.split('x')[0])*int(s.split('x')[1])
  largest_common_size = ''
  largest_area = 0
  common_sizes = list(set(supported_preview_sizes) & set(supported_video_sizes))
  for size in common_sizes:
    area = size_to_area(size)
    if area > largest_area:
      largest_area = area
      largest_common_size = size
  if not largest_common_size:
    raise AssertionError('No common size between Preview and Video!')
  logging.debug('Largest common size: %s', largest_common_size)
  return largest_common_size


def get_lowest_common_preview_video_size(
    supported_preview_sizes, supported_video_qualities, min_area=0):
  """Returns the common, smallest size above minimum in preview and video.

  Args:
    supported_preview_sizes: str; preview size (ex. '1920x1080')
    supported_video_qualities: str; video recording quality and id pair
    (ex. '480P:4', '720P:5'')
    min_area: int; optional filter to eliminate smaller sizes (ex. 640*480)
  Returns:
    smallest_common_size: str; smallest, common size between preview and video
    smallest_common_video_quality: str; video recording quality such as 480P
  """

  # Make dictionary on video quality and size according to compatibility
  supported_video_size_to_quality = {}
  for quality in supported_video_qualities:
    video_quality = quality.split(':')[0]
    if video_quality in VIDEO_QUALITY_SIZE:
      video_size = VIDEO_QUALITY_SIZE[video_quality]
      supported_video_size_to_quality[video_size] = video_quality
  logging.debug(
      'Supported video size to quality: %s', supported_video_size_to_quality)

  # Use areas of video sizes to find the smallest, common size
  size_to_area = lambda s: int(s.split('x')[0])*int(s.split('x')[1])
  smallest_common_size = ''
  smallest_area = float('inf')
  for size in supported_preview_sizes:
    if size in supported_video_size_to_quality:
      area = size_to_area(size)
      if smallest_area > area >= min_area:
        smallest_area = area
        smallest_common_size = size
  logging.debug('Lowest common size: %s', smallest_common_size)

  # Find video quality of resolution with resolution as key
  smallest_common_video_quality = (
      supported_video_size_to_quality[smallest_common_size])
  logging.debug(
      'Lowest common size video quality: %s', smallest_common_video_quality)

  return smallest_common_size, smallest_common_video_quality


def log_ffmpeg_version():
  """Logs the ffmpeg version being used."""

  ffmpeg_version_cmd = ('ffmpeg -version')
  p = subprocess.Popen(ffmpeg_version_cmd, shell=True, stdout=subprocess.PIPE)
  output, _ = p.communicate()
  if p.poll() != 0:
    raise error_util.CameraItsError('Error running ffmpeg version cmd.')
  decoded_output = output.decode('utf-8')
  logging.debug('ffmpeg version: %s', decoded_output.split(' ')[2])


def extract_key_frames_from_video(log_path, video_file_name):
  """Returns a list of extracted key frames.

  Ffmpeg tool is used to extract key frames from the video at path
  os.path.join(log_path, video_file_name).
  The extracted key frames will have the name video_file_name with "_key_frame"
  suffix to identify the frames for video of each quality. Since there can be
  multiple key frames, each key frame image will be differentiated with it's
  frame index. All the extracted key frames will be available in jpeg format
  at the same path as the video file.

  The run time flag '-loglevel quiet' hides the information from terminal.
  In order to see the detailed output of ffmpeg command change the loglevel
  option to 'info'.

  Args:
    log_path: path for video file directory.
    video_file_name: name of the video file.
  Returns:
    key_frame_files: a sorted list of files which contains a name per key
      frame. Ex: VID_20220325_050918_0_preview_1920x1440_key_frame_0001.png
  """
  ffmpeg_image_name = f'{os.path.splitext(video_file_name)[0]}_key_frame'
  ffmpeg_image_file_path = os.path.join(
      log_path, ffmpeg_image_name + '_%04d.png')
  cmd = ['ffmpeg',
         '-skip_frame',
         'nokey',
         '-i',
         os.path.join(log_path, video_file_name),
         '-vsync',
         'vfr',
         '-frame_pts',
         'true',
         ffmpeg_image_file_path,
         '-loglevel',
         'quiet',
        ]
  logging.debug('Extracting key frames from: %s', video_file_name)
  _ = subprocess.call(cmd,
                      stdin=subprocess.DEVNULL,
                      stdout=subprocess.DEVNULL,
                      stderr=subprocess.DEVNULL)
  arr = os.listdir(os.path.join(log_path))
  key_frame_files = []
  for file in arr:
    if '.png' in file and not os.path.isdir(file) and ffmpeg_image_name in file:
      key_frame_files.append(file)
  key_frame_files.sort()
  logging.debug('Extracted key frames: %s', key_frame_files)
  logging.debug('Length of key_frame_files: %d', len(key_frame_files))
  if not key_frame_files:
    raise AssertionError('No key frames extracted. Check source video.')

  return key_frame_files


def get_key_frame_to_process(key_frame_files):
  """Returns the key frame file from the list of key_frame_files.

  If the size of the list is 1 then the file in the list will be returned else
  the file with highest frame_index will be returned for further processing.

  Args:
    key_frame_files: A list of key frame files.
  Returns:
    key_frame_file to be used for further processing.
  """
  if not key_frame_files:
    raise AssertionError('key_frame_files list is empty.')
  key_frame_files.sort()
  return key_frame_files[-1]


def extract_all_frames_from_video(
    log_path, video_file_name, img_format, video_fps=None):
  """Extracts and returns a list of frames from a video using FFmpeg.

  Extract all frames from the video at path <log_path>/<video_file_name>.
  The extracted frames will have the name video_file_name with "_frame"
  suffix to identify the frames for video of each size. Each frame image
  will be differentiated with its frame index. All extracted rames will be
  available in the provided img_format format at the same path as the video.

  The run time flag '-loglevel quiet' hides the information from terminal.
  In order to see the detailed output of ffmpeg command change the loglevel
  option to 'info'.

  Args:
    log_path: str; directory containing video file.
    video_file_name: str; name of the video file.
    img_format: str; desired image format for export frames. ex. 'png'
    video_fps: str; fps of imported video.
  Returns:
    an ordered list of paths to the extracted frame images.
  """
  logging.debug('Extracting all frames')
  ffmpeg_image_name = f"{video_file_name.split('.')[0]}_frame"
  logging.debug('ffmpeg_image_name: %s', ffmpeg_image_name)
  ffmpeg_image_file_names = (
      f'{os.path.join(log_path, ffmpeg_image_name)}_%04d.{img_format}')
  if video_fps:
    cmd = [
        'ffmpeg', '-i', os.path.join(log_path, video_file_name),
        '-r', video_fps,  # force a constant frame rate for reliability
        ffmpeg_image_file_names, '-loglevel', 'quiet'
    ]
  else:
    cmd = [
        'ffmpeg', '-i', os.path.join(log_path, video_file_name),
        '-vsync', 'passthrough',  # prevents frame drops during decoding
        ffmpeg_image_file_names, '-loglevel', 'quiet'
    ]
  subprocess.call(cmd,
                  stdin=subprocess.DEVNULL,
                  stdout=subprocess.DEVNULL,
                  stderr=subprocess.DEVNULL)

  files = sorted(
      [file for file in os.listdir(log_path) if
       (file.endswith(img_format) and ffmpeg_image_name in file)])
  if not files:
    raise AssertionError('No frames extracted. Check source video.')

  return files


def extract_last_key_frame_from_recording(log_path, file_name):
  """Extract last key frame from recordings.

  Args:
    log_path: str; file location
    file_name: str file name for saved video

  Returns:
    numpy image of last key frame
  """
  key_frame_files = extract_key_frames_from_video(log_path, file_name)
  logging.debug('key_frame_files: %s', key_frame_files)

  # Get the last_key_frame file to process.
  last_key_frame_file = get_key_frame_to_process(key_frame_files)
  logging.debug('last_key_frame: %s', last_key_frame_file)

  # Convert last_key_frame to numpy array
  np_image = image_processing_utils.convert_image_to_numpy_array(
      os.path.join(log_path, last_key_frame_file))
  logging.debug('last key frame image shape: %s', np_image.shape)

  return np_image


def get_average_frame_rate(video_file_name_with_path):
  """Get average frame rate assuming variable frame rate video.

  Args:
    video_file_name_with_path: path to the video to be analyzed
  Returns:
    Float. average frames per second.
  """

  cmd = ['ffprobe',
         '-v',
         'quiet',
         '-show_streams',
         '-select_streams',
         'v:0',  # first video stream
         video_file_name_with_path
        ]
  logging.debug('Getting frame rate')
  raw_output = ''
  try:
    raw_output = subprocess.check_output(cmd,
                                         stdin=subprocess.DEVNULL,
                                         stderr=subprocess.STDOUT)
  except subprocess.CalledProcessError as e:
    raise AssertionError(str(e.output)) from e
  if raw_output:
    output = str(raw_output.decode('utf-8')).strip()
    logging.debug('ffprobe command %s output: %s', ' '.join(cmd), output)
    average_frame_rate_data = (
        re.search(r'avg_frame_rate=*([0-9]+/[0-9]+)', output)
        .group(INDEX_FIRST_SUBGROUP)
    )
    average_frame_rate = (int(average_frame_rate_data.split('/')[0]) /
                          int(average_frame_rate_data.split('/')[1]))
    logging.debug('Average FPS: %.4f', average_frame_rate)
    return average_frame_rate
  else:
    raise AssertionError('ffprobe failed to provide frame rate data')


def get_frame_deltas(video_file_name_with_path, timestamp_type='pts'):
  """Get list of time diffs between frames.

  Args:
    video_file_name_with_path: path to the video to be analyzed
    timestamp_type: 'pts' or 'dts'
  Returns:
    List of floats. Time diffs between frames in seconds.
  """

  cmd = ['ffprobe',
         '-show_entries',
         f'frame=pkt_{timestamp_type}_time',
         '-select_streams',
         'v',
         video_file_name_with_path
         ]
  logging.debug('Getting frame deltas')
  raw_output = ''
  try:
    raw_output = subprocess.check_output(cmd,
                                         stdin=subprocess.DEVNULL,
                                         stderr=subprocess.STDOUT)
  except subprocess.CalledProcessError as e:
    raise AssertionError(str(e.output)) from e
  if raw_output:
    output = str(raw_output.decode('utf-8')).strip().split('\n')
    deltas = []
    prev_time = None
    for line in output:
      if timestamp_type not in line:
        continue
      curr_time = float(re.search(r'time= *([0-9][0-9\.]*)', line)
                        .group(INDEX_FIRST_SUBGROUP))
      if prev_time is not None:
        deltas.append(curr_time - prev_time)
      prev_time = curr_time
    logging.debug('Frame deltas: %s', deltas)
    return deltas
  else:
    raise AssertionError('ffprobe failed to provide frame delta data')


def get_video_colorspace(log_path, video_file_name):
  """Get the video colorspace.

  Args:
    log_path: path for video file directory
    video_file_name: name of the video file
  Returns:
    video colorspace, e.g. BT.2020 or BT.709
  """

  cmd = ['ffprobe',
         '-show_streams',
         '-select_streams',
         'v:0',
         '-of',
         'json',
         '-i',
         os.path.join(log_path, video_file_name)
         ]
  logging.debug('Get the video colorspace')
  raw_output = ''
  try:
    raw_output = subprocess.check_output(cmd,
                                         stdin=subprocess.DEVNULL,
                                         stderr=subprocess.STDOUT)
  except subprocess.CalledProcessError as e:
    raise AssertionError(str(e.output)) from e

  logging.debug('raw_output: %s', raw_output)
  if raw_output:
    colorspace = ''
    output = str(raw_output.decode('utf-8')).strip().split('\n')
    logging.debug('output: %s', output)
    for line in output:
      logging.debug('line: %s', line)
      metadata = re.search(r'"color_space": ("[a-z0-9]*")', line)
      if metadata:
        colorspace = metadata.group(INDEX_FIRST_SUBGROUP)
    logging.debug('Colorspace: %s', colorspace)
    return colorspace
  else:
    raise AssertionError('ffprobe failed to provide color space')
