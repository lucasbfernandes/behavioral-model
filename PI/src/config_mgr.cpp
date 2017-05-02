/* Copyright 2013-present Barefoot Networks, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Antonin Bas (antonin@barefootnetworks.com)
 *
 */

#include <PI/frontends/proto/config_mgr.h>

#include "google/rpc/code.pb.h"

using Status = ::google::rpc::Status;
using Code = ::google::rpc::Code;

namespace pi {

namespace fe {

namespace proto {

class GnmiConfigMgrImp {
 public:
  Status config_get(const gnmi::GetRequest &request,
                    gnmi::GetResponse *response) const {
    (void) request; (void) response;
    Status status;
    status.set_code(Code::UNIMPLEMENTED);
    return status;
  }

  Status config_set(const gnmi::SetRequest &request,
                    gnmi::SetResponse *response) {
    (void) request; (void) response;
    Status status;
    status.set_code(Code::UNIMPLEMENTED);
    return status;
  }
};

GnmiConfigMgr::GnmiConfigMgr()
    : pimp(new GnmiConfigMgrImp()) { }

GnmiConfigMgr::~GnmiConfigMgr() = default;

Status
GnmiConfigMgr::config_get(const gnmi::GetRequest &request,
                          gnmi::GetResponse *response) const {
  return pimp->config_get(request, response);
}

Status
GnmiConfigMgr::config_set(const gnmi::SetRequest &request,
                          gnmi::SetResponse *response) {
  return pimp->config_set(request, response);
}

}  // namespace proto

}  // namespace fe

}  // namespace pi
