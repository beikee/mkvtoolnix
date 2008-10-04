/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   $Id$

   Subripper subtitle reader

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#include "os.h"

#include <boost/regex.hpp>
#include <string>

#include "common.h"
#include "pr_generic.h"
#include "p_textsubs.h"
#include "matroska.h"

using namespace libmatroska;

boost::regex textsubs_packetizer_c::s_re_remove_cr("\r", boost::regex::perl);
boost::regex textsubs_packetizer_c::s_re_remove_trailing_nl("\n+$", boost::regex::perl);
boost::regex textsubs_packetizer_c::s_re_translate_nl("\n", boost::regex::perl);

textsubs_packetizer_c::textsubs_packetizer_c(generic_reader_c *p_reader,
                                             track_info_c &p_ti,
                                             const char *codec_id,
                                             const void *global_data,
                                             int global_size,
                                             bool recode,
                                             bool is_utf8)
  throw (error_c)
  : generic_packetizer_c(p_reader, p_ti)
  , m_packetno(0)
  , m_cc_utf8(0)
  , m_global_data(new memory_c((unsigned char *)safememdup(global_data, global_size), global_size, true))
  , m_codec_id(codec_id)
  , m_recode(recode)
{
  if (m_recode)
    m_cc_utf8 = utf8_init((ti.sub_charset != "") || !is_utf8 ? ti.sub_charset : "UTF-8");

  set_track_type(track_subtitle);
  if (m_codec_id == MKV_S_TEXTUSF)
    set_default_compression_method(COMPRESSION_ZLIB);
}

textsubs_packetizer_c::~textsubs_packetizer_c() {
}

void
textsubs_packetizer_c::set_headers() {
  set_codec_id(m_codec_id);

  if (NULL != m_global_data->get())
    set_codec_private((unsigned char *)m_global_data->get(), m_global_data->get_size());

  generic_packetizer_c::set_headers();

  track_entry->EnableLacing(false);
}

int
textsubs_packetizer_c::process(packet_cptr packet) {
  if (0 > packet->duration) {
    mxwarn_tid(ti.fname, ti.id, Y("Ignoring an entry which starts after it ends.\n"));
    return FILE_STATUS_MOREDATA;
  }

  packet->duration_mandatory = true;

  string subs((char *)packet->data->get());

  subs = boost::regex_replace(subs, s_re_remove_cr,          "",     boost::match_default | boost::match_single_line);
  subs = boost::regex_replace(subs, s_re_remove_trailing_nl, "",     boost::match_default | boost::match_single_line);
  subs = boost::regex_replace(subs, s_re_translate_nl,       "\r\n", boost::match_default | boost::match_single_line);

  if (m_recode)
    subs = to_utf8(m_cc_utf8, subs);

  packet->data = memory_cptr(new memory_c((unsigned char *)subs.c_str(), subs.length(), false));

  add_packet(packet);

  return FILE_STATUS_MOREDATA;
}

connection_result_e
textsubs_packetizer_c::can_connect_to(generic_packetizer_c *src,
                                      string &error_message) {
  textsubs_packetizer_c *psrc = dynamic_cast<textsubs_packetizer_c *>(src);
  if (NULL == psrc)
    return CAN_CONNECT_NO_FORMAT;

  if (   ((NULL == ti.private_data) && (NULL != src->ti.private_data))
      || ((NULL != ti.private_data) && (NULL == src->ti.private_data))
      || (ti.private_size != src->ti.private_size)) {
    error_message = (boost::format(Y("The codec's private data does not match (lengths: %1% and %2%).")) % ti.private_size % src->ti.private_size).str();
    return CAN_CONNECT_MAYBE_CODECPRIVATE;
  }

  return CAN_CONNECT_YES;
}
