/*
 * Copyright (C) 2010 The Android Open Source Project
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

#define LOG_TAG "NetworkUtilities"

#include "NetworkUtilities.h"
#include <nativehelper/JNIHelp.h>
#include <nativehelper/JniConstants.h>
#include <nativehelper/ScopedLocalRef.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>

jobject sockaddrToInetAddress(JNIEnv* env, const sockaddr_storage& ss, jint* port) {

    const void* rawAddress;
    size_t addressLength;
    int sin_port = 0;
    int scope_id = 0;
    if (ss.ss_family == AF_INET) {
        const sockaddr_in& sin = reinterpret_cast<const sockaddr_in&>(ss);
        rawAddress = &sin.sin_addr.s_addr;
        addressLength = 4;
        sin_port = ntohs(sin.sin_port);
    } else {
        // We can't throw SocketException. We aren't meant to see bad addresses, so seeing one
        // really does imply an internal error.
        jniThrowExceptionFmt(env, "java/lang/IllegalArgumentException",
                             "sockaddrToInetAddress unsupported ss_family: %i", ss.ss_family);
        return NULL;
    }
    if (port != NULL) {
        *port = sin_port;
    }

    ScopedLocalRef<jbyteArray> byteArray(env, env->NewByteArray(addressLength));
    if (byteArray.get() == NULL) {
        return NULL;
    }
    env->SetByteArrayRegion(byteArray.get(), 0, addressLength,
            reinterpret_cast<const jbyte*>(rawAddress));

    static jmethodID getByAddressMethod = env->GetStaticMethodID(JniConstants::inetAddressClass,
            "getByAddress", "(Ljava/lang/String;[BI)Ljava/net/InetAddress;");
    if (getByAddressMethod == NULL) {
        return NULL;
    }
    return env->CallStaticObjectMethod(JniConstants::inetAddressClass, getByAddressMethod,
            NULL, byteArray.get(), scope_id);
}

static bool inetAddressToSockaddr(JNIEnv* env, jobject inetAddress, int port, sockaddr_storage& ss, socklen_t& sa_len, bool map) {
    memset(&ss, 0, sizeof(ss));
    sa_len = 0;

    if (inetAddress == NULL) {
        jniThrowNullPointerException(env, NULL);
        return false;
    }

    // Get holder.
    static jfieldID holderFid = env->GetFieldID(JniConstants::inetAddressClass, "holder", "Ljava/net/InetAddress$InetAddressHolder;");
    if (holderFid == NULL) {
        return false;
    }
    ScopedLocalRef<jobject> holder(env, env->GetObjectField(inetAddress, holderFid));
    // Get the address family.
    static jfieldID familyFid = env->GetFieldID(JniConstants::inetAddressHolderClass, "family", "I");
    if (familyFid == NULL) {
        return false;
    }
    ss.ss_family = env->GetIntField(holder.get(), familyFid);
    if (ss.ss_family == AF_UNSPEC) {
        sa_len = sizeof(ss.ss_family);
        return true; // Job done!
    }

    // Check this is an address family we support.
    if (ss.ss_family != AF_INET) {
        jniThrowExceptionFmt(env, "java/lang/IllegalArgumentException",
                "inetAddressToSockaddr bad family: %i", ss.ss_family);
        return false;
    }

    // Get the byte array that stores the IP address bytes in the InetAddress.
    static jmethodID bytesMid = env->GetMethodID(JniConstants::inetAddressClass, "getAddress", "()[B");
    if (bytesMid == NULL) {
        return false;
    }
    ScopedLocalRef<jbyteArray> addressBytes(env, reinterpret_cast<jbyteArray>(env->CallObjectMethod(inetAddress, bytesMid)));
    if (env->ExceptionCheck()) {
        return false;
    }
    if (addressBytes.get() == NULL) {
        jniThrowNullPointerException(env, NULL);
        return false;
    }

    // TODO: bionic's getnameinfo(3) seems to want its length parameter to be exactly
    // sizeof(sockaddr_in) for an IPv4 address and sizeof (sockaddr_in6) for an
    // IPv6 address. Fix getnameinfo so it accepts sizeof(sockaddr_storage), and
    // then unconditionally set sa_len to sizeof(sockaddr_storage) instead of having
    // to deal with this case by case.


    // Deal with Inet4Address instances.
    if (map) {
        abort();
    } else {
        // We should represent this Inet4Address as an IPv4 sockaddr_in.
        sockaddr_in& sin = reinterpret_cast<sockaddr_in&>(ss);
        sin.sin_port = htons(port);
        jbyte* dst = reinterpret_cast<jbyte*>(&sin.sin_addr.s_addr);
        env->GetByteArrayRegion(addressBytes.get(), 0, 4, dst);
        sa_len = sizeof(sockaddr_in);
    }
    return true;
}

bool inetAddressToSockaddrVerbatim(JNIEnv* env, jobject inetAddress, int port, sockaddr_storage& ss, socklen_t& sa_len) {
    return inetAddressToSockaddr(env, inetAddress, port, ss, sa_len, false);
}

bool inetAddressToSockaddr(JNIEnv* env, jobject inetAddress, int port, sockaddr_storage& ss, socklen_t& sa_len) {
    return inetAddressToSockaddr(env, inetAddress, port, ss, sa_len, true);
}

bool setBlocking(int fd, bool blocking) {
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
        return false;
    }

    if (!blocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    int rc = fcntl(fd, F_SETFL, flags);
    return (rc != -1);
}
