/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   MPEG ES (elementary stream) demultiplexer module

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#include "common/common_pch.h"

#include <cstring>

#include "common/codec.h"
#include "common/endian.h"
#include "common/error.h"
#include "common/mm_io_x.h"
#include "common/mpeg4_p2.h"
#include "common/id_info.h"
#include "input/r_mpeg_es.h"
#include "merge/input_x.h"
#include "merge/file_status.h"
#include "mpegparser/M2VParser.h"
#include "output/p_mpeg1_2.h"

#define PROBESIZE 4
#define READ_SIZE 1024 * 1024

int
mpeg_es_reader_c::probe_file(mm_io_c &in,
                             uint64_t size) {
  auto debug = debugging_option_c{"mpeg_es_detection|mpeg_es_probe"};

  if (PROBESIZE > size)
    return 0;

  try {
    memory_cptr af_buf = memory_c::alloc(READ_SIZE);
    unsigned char *buf = af_buf->get_buffer();
    in.setFilePointer(0);
    int num_read = in.read(buf, READ_SIZE);

    if (4 > num_read)
      return 0;

    in.setFilePointer(0);

    // MPEG TS starts with 0x47.
    if (0x47 == buf[0])
      return 0;

    // MPEG PS starts with 0x000001ba.
    uint32_t value = get_uint32_be(buf);
    if (MPEGVIDEO_PACKET_START_CODE == value)
      return 0;

    auto num_slice_start_codes_found = 0u;
    auto start_code_at_beginning     = mpeg_is_start_code(value);
    auto gop_start_code_found        = false;
    auto sequence_start_code_found   = false;
    auto ext_start_code_found        = false;
    auto picture_start_code_found    = false;

    auto ok                          = false;

    // Let's look for a MPEG ES start code inside the first 1 MB.
    int i;
    for (i = 4; i < num_read - 1; i++) {
      if (mpeg_is_start_code(value)) {
        mxdebug_if(debug, fmt::format("mpeg_es_detection: start code found; fourth byte: 0x{0:02x}\n", value & 0xff));

        if (MPEGVIDEO_SEQUENCE_HEADER_START_CODE == value)
          sequence_start_code_found = true;

        else if (MPEGVIDEO_PICTURE_START_CODE == value)
          picture_start_code_found = true;

        else if (MPEGVIDEO_GROUP_OF_PICTURES_START_CODE == value)
          gop_start_code_found = true;

        else if (MPEGVIDEO_EXT_START_CODE == value)
          gop_start_code_found = true;

        else if ((MPEGVIDEO_FIRST_SLICE_START_CODE <= value) && (MPEGVIDEO_LAST_SLICE_START_CODE >= value))
          ++num_slice_start_codes_found;

        ok = sequence_start_code_found
          && picture_start_code_found
          && (   ((num_slice_start_codes_found > 0) && start_code_at_beginning)
              || ((num_slice_start_codes_found > 0) && gop_start_code_found && ext_start_code_found)
              || (num_slice_start_codes_found >= 25));

        if (ok)
          break;
      }

      value <<= 8;
      value  |= buf[i];
    }

    mxdebug_if(debug,
               fmt::format("mpeg_es_detection: sequence {0} picture {1} gop {2} ext {3} #slice {4} start code at beginning {5}; examined {6} bytes\n",
                           sequence_start_code_found, picture_start_code_found, gop_start_code_found, ext_start_code_found, num_slice_start_codes_found, start_code_at_beginning, i));

    if (!ok)
      return 0;

    // Let's try to read one frame.
    M2VParser parser;
    parser.SetProbeMode();
    if (!read_frame(parser, in, READ_SIZE))
      return 0;

  } catch (...) {
    return 0;
  }

  return 1;
}

mpeg_es_reader_c::mpeg_es_reader_c(const track_info_c &ti,
                                   const mm_io_cptr &in)
  : generic_reader_c(ti, in)
{
}

void
mpeg_es_reader_c::read_headers() {
  try {
    M2VParser parser;

    // Let's find the first frame. We need its information like
    // resolution, MPEG version etc.
    parser.SetProbeMode();
    if (!read_frame(parser, *m_in, 1024 * 1024))
      throw mtx::input::header_parsing_x();

    m_in->setFilePointer(0);

    MPEG2SequenceHeader seq_hdr = parser.GetSequenceHeader();
    version                     = parser.GetMPEGVersion();
    interlaced                  = !seq_hdr.progressiveSequence;
    width                       = seq_hdr.width;
    height                      = seq_hdr.height;
    frame_rate                  = seq_hdr.progressiveSequence ? seq_hdr.frameOrFieldRate : seq_hdr.frameOrFieldRate * 2.0;
    aspect_ratio                = seq_hdr.aspectRatio;

    if ((0 >= aspect_ratio) || (1 == aspect_ratio))
      dwidth = width;
    else
      dwidth = (int)(height * aspect_ratio);
    dheight = height;

    MPEGChunk *raw_seq_hdr = parser.GetRealSequenceHeader();
    if (raw_seq_hdr)
      m_ti.m_private_data = memory_c::clone(raw_seq_hdr->GetPointer(), raw_seq_hdr->GetSize());
    else
      m_ti.m_private_data.reset();

    mxverb(2, fmt::format("mpeg_es_reader: version {0} width {1} height {2} FPS {3} AR {4}\n", version, width, height, frame_rate, aspect_ratio));

  } catch (mtx::mm_io::exception &) {
    throw mtx::input::open_x();
  }
  show_demuxer_info();
}

mpeg_es_reader_c::~mpeg_es_reader_c() {
}

void
mpeg_es_reader_c::create_packetizer(int64_t) {
  generic_packetizer_c *m2vpacketizer;
  if (!demuxing_requested('v', 0) || (NPTZR() != 0))
    return;

  m2vpacketizer = new mpeg1_2_video_packetizer_c(this, m_ti, version, frame_rate, width, height, dwidth, dheight, false);
  add_packetizer(m2vpacketizer);
  m2vpacketizer->set_video_interlaced_flag(interlaced);

  show_packetizer_info(0, m2vpacketizer);
}

file_status_e
mpeg_es_reader_c::read(generic_packetizer_c *,
                       bool) {
  int64_t bytes_to_read = std::min(static_cast<uint64_t>(READ_SIZE), m_size - m_in->getFilePointer());
  if (0 >= bytes_to_read)
    return flush_packetizers();

  memory_cptr chunk = memory_c::alloc(bytes_to_read);
  int64_t num_read  = m_in->read(chunk, bytes_to_read);

  if (0 < num_read) {
    chunk->set_size(num_read);
    PTZR0->process(new packet_t(chunk));
  }

  return bytes_to_read > num_read ? flush_packetizers() : FILE_STATUS_MOREDATA;
}

bool
mpeg_es_reader_c::read_frame(M2VParser &parser,
                             mm_io_c &in,
                             int64_t max_size) {
  auto af_buffer = memory_c::alloc(READ_SIZE);
  auto buffer    = af_buffer->get_buffer();
  int bytes_probed = 0;

  while (true) {
    auto state = parser.GetState();

    if (MPV_PARSER_STATE_FRAME == state)
      return true;

    if ((MPV_PARSER_STATE_EOS == state) || (MPV_PARSER_STATE_ERROR == state))
      return false;

    assert(MPV_PARSER_STATE_NEED_DATA == state);

    if ((max_size != -1) && (bytes_probed > max_size))
      return false;

    int bytes_read = in.read(buffer, std::min<int>(parser.GetFreeBufferSpace(), READ_SIZE));
    if (!bytes_read)
      return false;

    bytes_probed += bytes_read;

    parser.WriteData(buffer, bytes_read);
    parser.SetEOS();
  }
}

void
mpeg_es_reader_c::identify() {
  auto codec = fmt::format("mpg{0}", version);
  auto info  = mtx::id::info_c{};
  info.add(mtx::id::pixel_dimensions, fmt::format("{0}x{1}", width, height));

  id_result_container();
  id_result_track(0, ID_RESULT_TRACK_VIDEO, codec_c::get_name(codec, codec), info.get());
}
