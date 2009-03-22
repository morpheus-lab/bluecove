/**
 * BlueCove BlueZ module - Java library for Bluetooth on Linux
 *  Copyright (C) 2008 Vlad Skarzhevskyy
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing,
 *  software distributed under the License is distributed on an
 *  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *  KIND, either express or implied.  See the License for the
 *  specific language governing permissions and limitations
 *  under the License.
 *
 * @version $Id$
 */
#define CPP__FILE "BlueCoveBlueZ_L2CAP.c"

#include "BlueCoveBlueZ.h"

#include <sys/poll.h>
#include <bluetooth/l2cap.h>

//#define BLUECOVE_L2CAP_USE_MSG
// TODO Is this necessary to truncate data before calling socket functions? sockets preserve message boundaries.
//#define BLUECOVE_L2CAP_MTU_TRUNCATE

bool l2Get_options(JNIEnv* env, jlong handle, struct l2cap_options* opt);

JNIEXPORT jlong JNICALL Java_com_intel_bluetooth_BluetoothStackBlueZDBus_l2OpenClientConnectionImpl
  (JNIEnv* env, jobject peer, jlong localDeviceBTAddress, jlong address, jint channel, jboolean authenticate, jboolean encrypt, jint receiveMTU, jint transmitMTU, jint timeout) {
    debug("CONNECT connect, psm %d", channel);

    // allocate socket
    int handle = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (handle < 0) {
        throwIOException(env, "Failed to create socket. [%d] %s", errno, strerror(errno));
        return 0;
    }

    struct sockaddr_l2 localAddr;
    //bind local address
    localAddr.l2_family = AF_BLUETOOTH;
    localAddr.l2_psm = 0;
    //bacpy(&localAddr.l2_bdaddr, BDADDR_ANY);
    longToDeviceAddr(localDeviceBTAddress, &localAddr.l2_bdaddr);

    if (bind(handle, (struct sockaddr *)&localAddr, sizeof(localAddr)) < 0) {
        throwIOException(env, "Failed to bind socket. [%d] %s", errno, strerror(errno));
        close(handle);
        return 0;
    }

    // Set link mtu and security options
    struct l2cap_options opt;
    socklen_t opt_len = sizeof(opt);
    memset(&opt, 0, opt_len);
    opt.imtu = receiveMTU;
    opt.omtu = (transmitMTU > 0)?transmitMTU:L2CAP_DEFAULT_MTU;
    opt.flush_to = L2CAP_DEFAULT_FLUSH_TO;
    Edebug("L2CAP set imtu %i, omtu %i", opt.imtu, opt.omtu);

    if (setsockopt(handle, SOL_L2CAP, L2CAP_OPTIONS, &opt, opt_len) < 0) {
        throwIOException(env, "Failed to set L2CAP mtu options. [%d] %s", errno, strerror(errno));
        close(handle);
        return 0;
    }

    if (encrypt || authenticate) {
        int socket_opt = 0;
        socklen_t len = sizeof(socket_opt);
        if (getsockopt(handle, SOL_L2CAP, L2CAP_LM, &socket_opt, &len) < 0) {
            throwIOException(env, "Failed to read L2CAP link mode. [%d] %s", errno, strerror(errno));
            close(handle);
            return 0;
        }
        //if (master) {
        //  socket_opt |= L2CAP_LM_MASTER;
        //}
        if (authenticate) {
            socket_opt |= L2CAP_LM_AUTH;
            Edebug("L2CAP set authenticate");
        }
        if (encrypt) {
            socket_opt |= L2CAP_LM_ENCRYPT;
        }

        if ((socket_opt != 0) && setsockopt(handle, SOL_L2CAP, L2CAP_LM, &socket_opt, sizeof(socket_opt)) < 0) {
            throwIOException(env, "Failed to set L2CAP link mode. [%d] %s", errno, strerror(errno));
            close(handle);
            return 0;
        }
    }


    struct sockaddr_l2 remoteAddr;
    remoteAddr.l2_family = AF_BLUETOOTH;
    longToDeviceAddr(address, &remoteAddr.l2_bdaddr);
    remoteAddr.l2_psm = channel;

    // connect to server
    if (connect(handle, (struct sockaddr*)&remoteAddr, sizeof(remoteAddr)) != 0) {
        throwIOException(env, "Failed to connect. [%d] %s", errno, strerror(errno));
        close(handle);
        return 0;
    }
    debug("L2CAP connected, handle %li", handle);

    struct l2cap_options copt;
    if (!l2Get_options(env, handle, &copt)) {
        close(handle);
        return 0;
    }
    debug("L2CAP imtu %i, omtu %i", copt.imtu, copt.omtu);
    return handle;
}


JNIEXPORT void JNICALL Java_com_intel_bluetooth_BluetoothStackBlueZDBus_l2CloseClientConnection
  (JNIEnv* env, jobject peer, jlong handle) {
    debug("L2CAP disconnect, handle %li", handle);
    // Closing channel, further sends and receives will be disallowed.
    if (shutdown(handle, SHUT_RDWR) < 0) {
        debug("shutdown failed. [%d] %s", errno, strerror(errno));
    }
    if (close(handle) < 0) {
        throwIOException(env, "Failed to close socket. [%d] %s", errno, strerror(errno));
    }
}

bool l2Get_options(JNIEnv* env, jlong handle, struct l2cap_options* opt) {
    socklen_t opt_len = sizeof(*opt);
    if (getsockopt(handle, SOL_L2CAP, L2CAP_OPTIONS, opt, &opt_len) < 0) {
        throwIOException(env, "Failed to get L2CAP link mtu. [%d] %s", errno, strerror(errno));
        return false;
    }
    return true;
}


JNIEXPORT jboolean JNICALL Java_com_intel_bluetooth_BluetoothStackBlueZDBus_l2Ready
  (JNIEnv* env, jobject peer, jlong handle) {
    struct pollfd fds;
    int timeout = 10; // milliseconds
    fds.fd = handle;
    fds.events = POLLIN | POLLHUP | POLLERR;// | POLLRDHUP;
    fds.revents = 0;
    int poll_rc = poll(&fds, 1, timeout);
    if (poll_rc > 0) {
        if (fds.revents & (POLLHUP | POLLERR /*| POLLRDHUP*/)) {
            throwIOException(env, "Peer closed connection");
            return JNI_FALSE;
        } else if (fds.revents & POLLNVAL) {
            // this connection has been closed by invoking the close() method.
            throwIOException(env, "Connection closed");
            return JNI_FALSE;
        } else if (fds.revents & POLLIN) {
            return JNI_TRUE;
        }
    } else if (poll_rc == -1) {
        throwIOException(env, "Failed to read. [%d] %s", errno, strerror(errno));
        return JNI_FALSE;
    } else {
        //Edebug("poll: call timed out");
    }
    return JNI_FALSE;
}

JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothStackBlueZDBus_l2Receive
  (JNIEnv* env, jobject peer, jlong handle, jbyteArray inBuf) {
    if (inBuf == NULL) {
        throwRuntimeException(env, "Invalid argument");
        return;
    }
#ifdef BLUECOVE_L2CAP_MTU_TRUNCATE
    struct l2cap_options opt;
    if (!l2Get_options(env, handle, &opt)) {
       return 0;
    }
#endif //BLUECOVE_L2CAP_MTU_TRUNCATE
    bool dataReady = false;
    while(!dataReady) {
        struct pollfd fds;
        int timeout = 10; // milliseconds
        fds.fd = handle;
        fds.events = POLLIN | POLLHUP | POLLERR;// | POLLRDHUP;
        fds.revents = 0;
        int poll_rc = poll(&fds, 1, timeout);
        if (poll_rc > 0) {
            if (fds.revents & (POLLHUP | POLLERR /*| POLLRDHUP*/)) {
                throwIOException(env, "Peer closed connection");
                return 0;
            } else if (fds.revents & POLLNVAL) {
                // this connection has been closed by invoking the close() method.
                throwIOException(env, "Connection closed");
                return 0;
            } else if (fds.revents & POLLIN) {
                dataReady = true;
            }
        } else if (poll_rc == -1) {
            throwIOException(env, "Failed to read. [%d] %s", errno, strerror(errno));
            return 0;
        } else {
            //Edebug("poll: call timed out");
        }
        if(isCurrentThreadInterrupted(env, peer)) {
            return 0;
        }
    }

    jbyte *bytes = (*env)->GetByteArrayElements(env, inBuf, 0);
    if (bytes == NULL) {
        throwRuntimeException(env, "Invalid argument");
        return;
    }
    size_t inBufLen = (size_t)(*env)->GetArrayLength(env, inBuf);
    int readLen = inBufLen;

#ifdef BLUECOVE_L2CAP_MTU_TRUNCATE
    if (readLen > opt.imtu) {
        readLen = opt.imtu;
    }
#endif //BLUECOVE_L2CAP_MTU_TRUNCATE

#ifdef BLUECOVE_L2CAP_USE_MSG
    int flags = 0;
    iovec iov;
    msghdr msg;
    memset((void*)&iov, 0, sizeof(iov));
    memset((void*)&msg, 0, sizeof(msg));
    iov.iov_base = bytes;
    iov.iov_len = readLen;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    int count = recvmsg(handle, &msg, flags);
#else
    int count = recv(handle, (char *)bytes, readLen, 0);
#endif //BLUECOVE_L2CAP_USE_MSG
    if (count < 0) {
        throwIOException(env, "Failed to read. [%d] %s", errno, strerror(errno));
        count = 0;
    }

    (*env)->ReleaseByteArrayElements(env, inBuf, bytes, 0);
    debug("receive[] returns %i", count);
    return count;
}

JNIEXPORT void JNICALL Java_com_intel_bluetooth_BluetoothStackBlueZDBus_l2Send
  (JNIEnv* env, jobject peer, jlong handle, jbyteArray data, jint transmitMTU) {
#ifdef BLUECOVE_L2CAP_MTU_TRUNCATE
    struct l2cap_options opt;
    if (!l2Get_options(env, handle, &opt)) {
        return;
    }
#endif //BLUECOVE_L2CAP_MTU_TRUNCATE

    if (data == NULL) {
        throwRuntimeException(env, "Invalid argument");
        return;
    }
    jbyte *bytes = (*env)->GetByteArrayElements(env, data, 0);
    if (bytes == NULL) {
        throwRuntimeException(env, "Invalid argument");
        return;
    }
    int len = (int)(*env)->GetArrayLength(env, data);
    if (len > transmitMTU) {
		len = transmitMTU;
	}

#ifdef BLUECOVE_L2CAP_MTU_TRUNCATE
    if (len > opt.omtu) {
        len = opt.omtu;
    }
#endif //BLUECOVE_L2CAP_MTU_TRUNCATE

    int count = send(handle, (char *)bytes, len, 0);
    if (count < 0) {
        throwIOException(env, "Failed to write. [%d] %s", errno, strerror(errno));
    }
    (*env)->ReleaseByteArrayElements(env, data, bytes, 0);
}

JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothStackBlueZDBus_l2GetReceiveMTU
  (JNIEnv* env, jobject peer, jlong handle) {
    struct l2cap_options opt;
    if (l2Get_options(env, handle, &opt)) {
        return opt.imtu;
    } else {
        return 0;
    }
}

JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothStackBlueZDBus_l2GetTransmitMTU
  (JNIEnv* env, jobject peer, jlong handle) {
    struct l2cap_options opt;
    if (l2Get_options(env, handle, &opt)) {
        return opt.omtu;
    } else {
        return 0;
    }
}

JNIEXPORT jlong JNICALL Java_com_intel_bluetooth_BluetoothStackBlueZDBus_l2RemoteAddress
  (JNIEnv* env, jobject peer, jlong handle) {
    struct sockaddr_l2 remoteAddr;
    socklen_t len = sizeof(remoteAddr);
    if (getpeername(handle, (struct sockaddr*)&remoteAddr, &len) < 0) {
        throwIOException(env, "Failed to get L2CAP (%i) peer name. [%d] %s", (int)handle, errno, strerror(errno));
        return -1;
    }
    return deviceAddrToLong(&remoteAddr.l2_bdaddr);
}

JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothStackBlueZDBus_l2GetSecurityOpt
  (JNIEnv* env, jobject peer, jlong handle, jint expected) {
    int socket_opt = 0;
    socklen_t len = sizeof(socket_opt);
    if (getsockopt(handle, SOL_L2CAP, L2CAP_LM, &socket_opt, &len) < 0) {
        throwIOException(env, "Failed to get L2CAP (%i) link mode. [%d] %s", (int)handle, errno, strerror(errno));
        return 0;
    }
    bool encrypted = socket_opt &  (L2CAP_LM_ENCRYPT | L2CAP_LM_SECURE);
    bool authenticated = socket_opt & L2CAP_LM_AUTH;
    if (authenticated) {
        return NOAUTHENTICATE_NOENCRYPT;
    }
    if (encrypted) {
        return AUTHENTICATE_ENCRYPT;
    } else {
        return AUTHENTICATE_NOENCRYPT;
    }
}
