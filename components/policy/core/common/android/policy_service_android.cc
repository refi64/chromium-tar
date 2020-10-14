// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/policy/core/common/android/policy_service_android.h"

#include "base/android/jni_android.h"
#include "components/policy/android/jni_headers/PolicyService_jni.h"

namespace policy {
namespace android {

// PolicyServiceAndroid

PolicyServiceAndroid::PolicyServiceAndroid(PolicyService* policy_service)
    : policy_service_(policy_service) {}
PolicyServiceAndroid::~PolicyServiceAndroid() = default;

void PolicyServiceAndroid::AddObserver(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& caller) {
  policy_service_->AddObserver(POLICY_DOMAIN_CHROME, this);
}

void PolicyServiceAndroid::RemoveObserver(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& caller) {
  policy_service_->RemoveObserver(POLICY_DOMAIN_CHROME, this);
}

void PolicyServiceAndroid::OnPolicyServiceInitialized(PolicyDomain domain) {
  DCHECK_EQ(POLICY_DOMAIN_CHROME, domain);
  DCHECK(java_ref_);
  Java_PolicyService_onPolicyServiceInitialized(
      base::android::AttachCurrentThread(),
      base::android::ScopedJavaLocalRef<jobject>(java_ref_));
}

bool PolicyServiceAndroid::IsInitializationComplete(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& caller) {
  return policy_service_->IsInitializationComplete(POLICY_DOMAIN_CHROME);
}

base::android::ScopedJavaLocalRef<jobject>
PolicyServiceAndroid::GetJavaObject() {
  JNIEnv* env = base::android::AttachCurrentThread();
  if (!java_ref_) {
    java_ref_.Reset(
        Java_PolicyService_Constructor(env, reinterpret_cast<intptr_t>(this)));
  }
  return base::android::ScopedJavaLocalRef<jobject>(java_ref_);
}

}  // namespace android
}  // namespace policy
