/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "library_namespaces.h"

#include "android-base/result-gmock.h"
#include "gtest/gtest.h"
#include "public_libraries.h"

namespace android {
namespace nativeloader {
namespace {

using ::android::base::testing::HasError;
using ::android::base::testing::HasValue;
using ::android::base::testing::WithMessage;
using ::testing::StartsWith;

static ApiDomain GetProductApiDomain(ApiDomain fallback_domain) {
  // GetApiDomainFromPath returns API_DOMAIN_PRODUCT only if the device is
  // trebleized and has an unbundled product partition.
  return is_product_treblelized() ? API_DOMAIN_PRODUCT : fallback_domain;
}

TEST(LibraryNamespacesTest, TestGetApiDomainFromPath) {
  EXPECT_EQ(GetApiDomainFromPath("/data/somewhere"), API_DOMAIN_DEFAULT);
  EXPECT_EQ(GetApiDomainFromPath("/system/somewhere"), API_DOMAIN_SYSTEM);
  EXPECT_EQ(GetApiDomainFromPath("/system_ext/somewhere"), API_DOMAIN_SYSTEM);
  EXPECT_EQ(GetApiDomainFromPath("/systemext/somewhere"), API_DOMAIN_DEFAULT);
  EXPECT_EQ(GetApiDomainFromPath("/product/somewhere"), GetProductApiDomain(API_DOMAIN_DEFAULT));
  EXPECT_EQ(GetApiDomainFromPath("/vendor/somewhere"), API_DOMAIN_VENDOR);
  EXPECT_EQ(GetApiDomainFromPath("/system/product/somewhere"),
            GetProductApiDomain(API_DOMAIN_SYSTEM));
  EXPECT_EQ(GetApiDomainFromPath("/system/vendor/somewhere"), API_DOMAIN_VENDOR);

  EXPECT_EQ(GetApiDomainFromPath(""), API_DOMAIN_DEFAULT);
  EXPECT_EQ(GetApiDomainFromPath("/"), API_DOMAIN_DEFAULT);
  EXPECT_EQ(GetApiDomainFromPath("product/somewhere"), API_DOMAIN_DEFAULT);
  EXPECT_EQ(GetApiDomainFromPath("/product"), API_DOMAIN_DEFAULT);
  EXPECT_EQ(GetApiDomainFromPath("/product/"), GetProductApiDomain(API_DOMAIN_DEFAULT));
  EXPECT_EQ(GetApiDomainFromPath(":/product/"), API_DOMAIN_DEFAULT);

  EXPECT_EQ(GetApiDomainFromPath("/data/somewhere:/product/somewhere"), API_DOMAIN_DEFAULT);
  EXPECT_EQ(GetApiDomainFromPath("/vendor/somewhere:/product/somewhere"), API_DOMAIN_VENDOR);
  EXPECT_EQ(GetApiDomainFromPath("/product/somewhere:/vendor/somewhere"),
            GetProductApiDomain(API_DOMAIN_DEFAULT));
}

TEST(LibraryNamespacesTest, TestGetApiDomainFromPathList) {
  EXPECT_THAT(GetApiDomainFromPathList("/data/somewhere"), HasValue(API_DOMAIN_DEFAULT));
  EXPECT_THAT(GetApiDomainFromPathList("/system/somewhere"), HasValue(API_DOMAIN_DEFAULT));
  EXPECT_THAT(GetApiDomainFromPathList("/system_ext/somewhere"), HasValue(API_DOMAIN_DEFAULT));
  EXPECT_THAT(GetApiDomainFromPathList("/product/somewhere"),
              HasValue(GetProductApiDomain(API_DOMAIN_DEFAULT)));
  EXPECT_THAT(GetApiDomainFromPathList("/vendor/somewhere"), HasValue(API_DOMAIN_VENDOR));
  EXPECT_THAT(GetApiDomainFromPathList("/system/product/somewhere"),
              HasValue(GetProductApiDomain(API_DOMAIN_DEFAULT)));
  EXPECT_THAT(GetApiDomainFromPathList("/system/vendor/somewhere"), HasValue(API_DOMAIN_VENDOR));

  EXPECT_THAT(GetApiDomainFromPathList(""), HasValue(API_DOMAIN_DEFAULT));
  EXPECT_THAT(GetApiDomainFromPathList(":"), HasValue(API_DOMAIN_DEFAULT));
  EXPECT_THAT(GetApiDomainFromPathList(":/vendor/somewhere"), HasValue(API_DOMAIN_VENDOR));
  EXPECT_THAT(GetApiDomainFromPathList("/vendor/somewhere:"), HasValue(API_DOMAIN_VENDOR));

  EXPECT_THAT(GetApiDomainFromPathList("/data/somewhere:/product/somewhere"),
              HasValue(GetProductApiDomain(API_DOMAIN_DEFAULT)));
  EXPECT_THAT(GetApiDomainFromPathList("/system/somewhere:/product/somewhere"),
              HasValue(GetProductApiDomain(API_DOMAIN_DEFAULT)));
  EXPECT_THAT(GetApiDomainFromPathList("/product/somewhere:/system/somewhere"),
              HasValue(GetProductApiDomain(API_DOMAIN_DEFAULT)));
  EXPECT_THAT(GetApiDomainFromPathList("/data/somewhere:/vendor/somewhere"),
              HasValue(API_DOMAIN_VENDOR));
  EXPECT_THAT(GetApiDomainFromPathList("/system/somewhere:/vendor/somewhere"),
              HasValue(API_DOMAIN_VENDOR));
  EXPECT_THAT(GetApiDomainFromPathList("/vendor/somewhere:/system/somewhere"),
              HasValue(API_DOMAIN_VENDOR));

  if (GetProductApiDomain(API_DOMAIN_DEFAULT) == API_DOMAIN_PRODUCT) {
    EXPECT_THAT(
        GetApiDomainFromPathList("/vendor/somewhere:/product/somewhere"),
        HasError(WithMessage(StartsWith("Path list crosses vendor/product partition boundaries"))));
    EXPECT_THAT(
        GetApiDomainFromPathList("/system/somewhere:/product/somewhere:/vendor/somewhere"),
        HasError(WithMessage(StartsWith("Path list crosses vendor/product partition boundaries"))));
  }
}

TEST(LibraryNamespacesTest, TestIsPartitionNativeLibPath) {
  EXPECT_TRUE(IsPartitionNativeLibPath("/system/lib/libfoo.so"));
  EXPECT_TRUE(IsPartitionNativeLibPath("/system/lib64/libfoo.so"));
  EXPECT_TRUE(IsPartitionNativeLibPath("/system_ext/lib64/libfoo.so"));
  EXPECT_TRUE(IsPartitionNativeLibPath("/product/lib64/libfoo.so"));
  EXPECT_TRUE(IsPartitionNativeLibPath("/vendor/lib64/libfoo.so"));
  EXPECT_TRUE(IsPartitionNativeLibPath("/vendor/lib64/hw/libhw.so"));
  EXPECT_TRUE(IsPartitionNativeLibPath("/system/system_ext/lib64/libfoo.so"));
  EXPECT_TRUE(IsPartitionNativeLibPath("/system/product/lib64/libfoo.so"));
  EXPECT_TRUE(IsPartitionNativeLibPath("/system/vendor/lib64/libfoo.so"));

  EXPECT_FALSE(IsPartitionNativeLibPath("/data/app/~~hash==/myapp-hash==/lib/arm64/libapp.so"));
  EXPECT_FALSE(
      IsPartitionNativeLibPath("/system/app/PrintSpooler/lib/arm64/libprintspooler_jni.so"));
  EXPECT_FALSE(IsPartitionNativeLibPath("/product/app/Camera2/lib/arm64/libjni_jpegutil.so"));
}

}  // namespace
}  // namespace nativeloader
}  // namespace android
