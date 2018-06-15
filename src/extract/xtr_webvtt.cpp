/*
   mkvextract -- extract tracks from Matroska files into other files

   Distributed under the GPL v2
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   extracts tracks from Matroska files into other files

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#include "common/common_pch.h"

#include "common/codec.h"
#include "common/ebml.h"
#include "common/webvtt.h"
#include "common/strings/editing.h"
#include "common/strings/formatting.h"
#include "extract/xtr_webvtt.h"

using namespace libmatroska;

xtr_webvtt_c::xtr_webvtt_c(const std::string &codec_id,
                     int64_t tid,
                     track_spec_t &tspec)
  : xtr_base_c{codec_id, tid, tspec}
{
}

void
xtr_webvtt_c::create_file(xtr_base_c *master,
                          KaxTrackEntry &track) {
  auto priv = FindChild<KaxCodecPrivate>(&track);
  if (!priv)
    mxerror(boost::format(Y("Track %1% with the CodecID '%2%' is missing the \"codec private\" element and cannot be extracted.\n")) % m_tid % m_codec_id);

  xtr_base_c::create_file(master, track);

  m_out->write_bom("UTF-8");

  auto global = chomp(normalize_line_endings(decode_codec_private(priv)->to_string())) + "\n";
  m_out->write(global);
}

void
xtr_webvtt_c::handle_frame(xtr_frame_t &f) {
  ++m_num_entries;

  if (-1 == f.duration) {
    mxwarn(boost::format(Y("Track %1%: Subtitle entry number %2% is missing its duration. Assuming a duration of 1s.\n")) % m_tid % m_num_entries);
    f.duration = 1000000000;
  }

  std::string label, settings_list, local_blocks;

  if (f.additions) {
    auto &block_more    = GetChild<KaxBlockMore>(f.additions);
    auto block_addition = FindChild<KaxBlockAdditional>(block_more);

    if (block_addition) {
      auto content = std::string{reinterpret_cast<char const *>(block_addition->GetBuffer()), static_cast<std::string::size_type>(block_addition->GetSize())};
      auto lines   = split(chomp(normalize_line_endings(content)), "\n", 3);

      if ((lines.size() > 0) && !lines[0].empty())
        settings_list = " "s + boost::trim_copy(lines[0]);

      if ((lines.size() > 1) && !lines[1].empty())
        label = boost::trim_copy(lines[1]) + "\n";

      if ((lines.size() > 2) && !lines[2].empty())
        local_blocks = chomp(lines[2]) + "\n\n";
    }
  }

  auto content = chomp(normalize_line_endings(f.frame->to_string())) + "\n";
  content      = webvtt_parser_c::adjust_embedded_timestamps(content, timestamp_c::ns(f.timestamp));
  content      = (boost::format("\n%1%%2%%3% --> %4%%5%\n%6%")
                  % local_blocks % label
                  % format_timestamp(f.timestamp, 3) % format_timestamp(f.timestamp + f.duration, 3)
                  % settings_list % content).str();

  m_out->write(content);
}
