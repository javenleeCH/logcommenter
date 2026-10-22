# SPDX-License-Identifier: MIT
#
# SPDX-FileCopyrightText: Copyright (c) 2019-2023 Lars Melchior and contributors

set(CPM_DOWNLOAD_VERSION 0.43.2)
set(CPM_HASH_SUM "49a3bef91ceb65bb66d57255e12d1ffd22abc2f6408fa9fe4534c544a2f232aa")

if(CPM_PATH)
  set(CPM_DOWNLOAD_LOCATION "${CPM_PATH}/CPM.cmake")
elseif(DEFINED ENV{CPM_PATH})
  file(TO_CMAKE_PATH "$ENV{CPM_PATH}/CPM.cmake" CPM_DOWNLOAD_LOCATION)
elseif(CPM_SOURCE_CACHE)
  set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
  set(CPM_DOWNLOAD_LOCATION "$ENV{CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
else()
  set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
endif()

# Expand relative path. This is important if the provided path contains a tilde (~)
get_filename_component(CPM_DOWNLOAD_LOCATION ${CPM_DOWNLOAD_LOCATION} ABSOLUTE)

# TLS 校验开关：与 llvm-project 下载共用同一个 CPM_DOWNLOAD_TLS_VERIFY。
# 项目侧 -DLOGC_DISABLE_SSL=ON 会把它置为 OFF（代理/自签证书环境）。
if(NOT DEFINED CPM_DOWNLOAD_TLS_VERIFY)
  set(CPM_DOWNLOAD_TLS_VERIFY ON)
endif()

file(DOWNLOAD
     https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
     ${CPM_DOWNLOAD_LOCATION} EXPECTED_HASH SHA256=${CPM_HASH_SUM}
     TLS_VERIFY ${CPM_DOWNLOAD_TLS_VERIFY}
)

include(${CPM_DOWNLOAD_LOCATION})
