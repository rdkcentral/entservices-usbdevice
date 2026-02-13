# If not stated otherwise in this file or this component's Licenses.txt file the
# following copyright and licenses apply:
#
# Copyright 2016 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#
# features
#


add_definitions(-DRDK_LOG_MILESTONE)

option(PLUGIN_TELEMETRY "PLUGIN_TELEMETRY" ON)
option(PLUGIN_CONTINUEWATCHING "PLUGIN_CONTINUEWATCHING" ON)


#add_definitions (-DCLIENT_VERSION_STRING)=\\\"$(VERSION_FULL_VALUE)\\\"
#add_definitions (-DSTB_VERSION_STRING)=\\\"$(FULL_VERSION_NAME_VALUE)\\\"
#add_definitions (-DSTB_TIMESTAMP_STRING)=\\\"$(STB_TIMESTAMP_VALUE)\\\"

#add_definitions (-DHAS_API_TTSSETTINGSSERVICE)
#add_definitions (-DHAS_API_TTSSESSIONSERVICE)
#add_definitions (-DHAS_API_TTSRESOURCESERVICE)
add_definitions (-DPLUGIN_CONTINUEWATCHING)
option(PLUGIN_CONTINUEWATCHING "PLUGIN_CONTINUEWATCHING" ON)

if(PLUGIN_CONTINUEWATCHING)
    if(CONTINUEWATCHING_DISABLE_SECAPI)
        add_definitions (-DDISABLE_SECAPI)
    endif()
endif()


if(PLUGIN_CONTINUEWATCHING)
    if(CONTINUEWATCHING_DISABLE_SECAPI)
        add_definitions (-DDISABLE_SECAPI)
    endif()
endif()

if (BUILD_ENABLE_TELEMETRY_LOGGING)
    message("Building with telemetry logging")
    add_definitions (-DENABLE_TELEMETRY_LOGGING)
endif()
