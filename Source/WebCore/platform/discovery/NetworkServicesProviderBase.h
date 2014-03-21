/*
 * Copyright (C) Canon Inc. 2014
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted, provided that the following conditions
 * are required to be met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Canon Inc. nor the names of 
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY CANON INC. AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL CANON INC. AND ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef NetworkServicesProviderBase_h
#define NetworkServicesProviderBase_h

#if ENABLE(DISCOVERY)

#include "NetworkServiceDescription.h"
#include "Timer.h"

#include <wtf/Vector.h>

namespace WebCore {

class NetworkServicesProviderClient;

class NetworkServicesProviderBase {
public:
    virtual void startUpdating() = 0;
    virtual void stopUpdating() = 0;

    NetworkServiceDescription* lastNetworkServiceDescription() const { return m_currentDesc; }

    void addServiceDescription(NetworkServiceDescription*);
    void addServiceDescription(const char*, const char*, const char*, const char*, const char*, const char* eventSubURL = 0);

    bool contains(NetworkServiceDescription*);

    void sendDiscoveredServices();

    virtual void subscribeEventNotification(const String&) { }
    void setServiceEventSubURL(const String&, const String&);

protected: 
    NetworkServicesProviderBase(NetworkServicesProviderClient *);

    NetworkServiceDescription* getServiceDescriptionById(const String& id) const;
    void removeServiceDescription(const char*);

    virtual void notifyDiscoveryFinished();
    virtual void notifyNetworkServiceChanged(NetworkServiceDescription*);

    NetworkServicesProviderClient *m_client;
    Vector<NetworkServiceDescription*> m_descriptions;

private:
    NetworkServiceDescription* m_currentDesc;
};

} // namespace WebCore

#endif // ENABLE(DISCOVERY)

#endif // NetworkServicesProviderBase_h
