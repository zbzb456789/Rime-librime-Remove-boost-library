//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2014-01-04 GONG Chen <chen.sst@gmail.com>
//

#include <rime_api.h>
#include <rime/common.h>
#include <rime/registry.h>

#include "spelling_filter.h"

using namespace rime;

static void rime_SpellingFilter_initialize() {
  LOG(INFO) << "registering components from module 'SpellingFilter'.";
  Registry& r = Registry::instance();
  r.Register("spelling_filter", new Component<SpellingFilter::SpellingFilter>);
}

static void rime_SpellingFilter_finalize() {}

RIME_REGISTER_MODULE(SpellingFilter)
