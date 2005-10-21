/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   $Id$

   class definition for the cluster helper

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#ifndef __CLUSTER_HELPER_C
#define __CLUSTER_HELPER_C

#include <vector>

#include "os.h"

#include <matroska/KaxBlock.h>
#include <matroska/KaxCluster.h>

#include "mm_io.h"
#include "pr_generic.h"

using namespace std;

#define RND_TIMECODE_SCALE(a) (irnd((double)(a) / \
                                    (double)((int64_t)timecode_scale)) * \
                               (int64_t)timecode_scale)

struct ch_contents_t {
  KaxCluster *cluster;
  vector<packet_cptr> packets;
  bool is_referenced, rendered;

  ch_contents_t():
    cluster(NULL),
    is_referenced(false),
    rendered(false) {
  }

  ~ch_contents_t() {
    delete cluster;
  }
};

struct render_groups_t {
  vector<KaxBlockBlob *> groups;
  vector<int64_t> durations;
  generic_packetizer_c *source;
  bool more_data, duration_mandatory;
};

struct split_point_t {
  enum split_point_type_e {
    SPT_DURATION,
    SPT_SIZE,
    SPT_TIMECODE,
    SPT_CHAPTER
  };

  int64_t m_point;
  split_point_type_e m_type;
  bool m_use_once;

  split_point_t(int64_t point, split_point_type_e type, bool use_once):
    m_point(point), m_type(type), m_use_once(use_once) { }
};

class cluster_helper_c {
private:
  vector<ch_contents_t *> clusters;
  int cluster_content_size;
  int64_t max_timecode_and_duration;
  int64_t last_cluster_tc, num_cue_elements, header_overhead;
  int64_t packet_num, timecode_offset, *last_packets;
  int64_t bytes_in_file, first_timecode_in_file;
  mm_io_c *out;

  vector<split_point_t> split_points;
  vector<split_point_t>::iterator current_split_point;

public:
  cluster_helper_c();
  virtual ~cluster_helper_c();

  void set_output(mm_io_c *nout);
  void add_cluster(KaxCluster *cluster);
  KaxCluster *get_cluster();
  void add_packet(packet_cptr packet);
  int64_t get_timecode();
  packet_cptr get_packet(int num);
  int get_packet_count();
  int render(bool flush = false);
  int free_ref(int64_t ref_timecode, generic_packetizer_c *source);
  int free_clusters();
  int get_cluster_content_size();
  int64_t get_duration();
  int64_t get_first_timecode_in_file() {
    return first_timecode_in_file;
  }

  void add_split_point(const split_point_t &split_point);
  bool splitting() {
    return !split_points.empty();
  }

private:
  int find_cluster(KaxCluster *cluster);
  ch_contents_t *find_packet_cluster(int64_t ref_timecode,
                                     generic_packetizer_c *source);
  packet_cptr find_packet(int64_t ref_timecode,
                          generic_packetizer_c *source);
  void free_contents(ch_contents_t *clstr);
  void check_clusters(int num);
  bool all_references_resolved(ch_contents_t *cluster);
  void set_duration(render_groups_t *rg);
  int render_cluster(ch_contents_t *clstr);
};

extern cluster_helper_c *cluster_helper;

#endif // __CLUSTER_HELPER_C
