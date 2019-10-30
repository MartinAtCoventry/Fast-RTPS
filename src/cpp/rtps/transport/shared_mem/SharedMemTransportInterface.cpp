// Copyright 2019 Proyectos y Sistemas de Mantenimiento SL (eProsima).
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <fastdds/rtps/transport/TransportInterface.h>
#include <fastdds/rtps/transport/shared_mem/SharedMemTransportInterface.h>
#include "SharedMemSenderResource.hpp"

#include <fastrtps/rtps/messages/CDRMessage.h>
#include <fastrtps/log/Log.h>
#include <fastrtps/utils/Semaphore.h>
#include <fastrtps/utils/IPLocator.h>

#include <utility>
#include <cstring>
#include <algorithm>
#include <chrono>

using namespace std;

namespace eprosima {
namespace fastdds {
namespace rtps {

using LocatorList_t = fastrtps::rtps::LocatorList_t;
using Locator_t = fastrtps::rtps::Locator_t;
using octet = fastrtps::rtps::octet;
using IPLocator = fastrtps::rtps::IPLocator;
using SenderResource = fastrtps::rtps::SenderResource;
using LocatorSelectorEntry = fastrtps::rtps::LocatorSelectorEntry;
using LocatorSelector = fastrtps::rtps::LocatorSelector;
using PortParameters = fastrtps::rtps::PortParameters;
using Log = dds::Log;

SharedMemTransportInterface::SharedMemTransportInterface()
	: TransportInterface(LOCATOR_KIND_SHMEM)
	, mSendBufferSize(0)
	, mReceiveBufferSize(0)
{
}

SharedMemTransportInterface::~SharedMemTransportInterface()
{
}

void SharedMemTransportInterface::clean()
{
	assert(input_channels_.size() == 0);
}

bool SharedMemTransportInterface::CloseInputChannel(
		const Locator_t& locator)
{
	std::vector<SharedMemChannelResource*> channel_resources;

	{
		std::lock_guard<std::recursive_mutex> lock(input_channels_mutex_);

		if (!IsInputChannelOpen(locator))
			return false;

		channel_resources = std::move(input_channels_);
	}

	// We now disable and release the channels
	for (SharedMemChannelResource* channel : channel_resources)
	{
		channel->disable();
		channel->release();
		channel->clear();
		delete channel;
	}

	return true;
}

bool SharedMemTransportInterface::DoInputLocatorsMatch(
		const Locator_t& left, 
		const Locator_t& right) const
{
	return IPLocator::getPhysicalPort(left) == IPLocator::getPhysicalPort(right);
}

bool SharedMemTransportInterface::init()
{
	try
	{
		shared_mem_ = std::make_shared<eProsimaSharedMem>();
	}
	catch (std::exception& e)
	{
		logError(RTPS_MSG_OUT, e.what());
		return false;
	}

	return true;
}

bool SharedMemTransportInterface::IsInputChannelOpen(
		const Locator_t& locator) const
{
	std::lock_guard<std::recursive_mutex> lock(input_channels_mutex_);

	return IsLocatorSupported(locator) && (std::find_if(
		input_channels_.begin(), input_channels_.end(),
		[&](const SharedMemChannelResource* resource) { return locator == resource->locator(); }) != input_channels_.end());
}

bool SharedMemTransportInterface::IsLocatorSupported(
		const Locator_t& locator) const
{
	return locator.kind == transport_kind_;
}

SharedMemChannelResource* SharedMemTransportInterface::CreateInputChannelResource(
		const Locator_t& locator,
		bool is_multicast,
		uint32_t maxMsgSize,
		TransportReceiverInterface* receiver)
{
	//TODO:Check exceptions
	return new SharedMemChannelResource(this, shared_mem_->createReader(locator, maxMsgSize), maxMsgSize, locator, receiver);
}

bool SharedMemTransportInterface::OpenOutputChannel(
		SendResourceList& sender_resource_list,
		const Locator_t& locator)
{
	if (!IsLocatorSupported(locator))
	{
		return false;
	}

	// We try to find a SenderResource that can be reuse to this locator.
	// Note: This is done in this level because if we do in NetworkFactory level, we have to mantain what transport
	// already reuses a SenderResource.
	for (auto& sender_resource : sender_resource_list)
	{
		SharedMemSenderResource* sm_sender_resource = SharedMemSenderResource::cast(*this, sender_resource.get());

		if (sm_sender_resource)
		{
			return true;
		}
	}

	try
	{
		//eProsimaSharedMem shared_mem = eProsimaSharedMem(eProsimaSharedMem::ChannelType::WRITER, locator, mSendBufferSize);
		sender_resource_list.emplace_back(
			static_cast<SenderResource*>(new SharedMemSenderResource(*this, shared_mem_->createWritter())));
	}
	catch (std::exception&)
	{
		//	(void)e;
		/* TODO Que hacer?
		logError(RTPS_MSG_OUT, "UDPTransport Error binding at port: (" << IPLocator::getPhysicalPort(locator) << ")"
			<< " with msg: " << e.what());
		for (auto& socket : mOutputSockets)
		{
			delete socket;
		}
		mOutputSockets.clear();
		*/
		return false;
	}

	return true;
}

Locator_t SharedMemTransportInterface::RemoteToMainLocal(
		const Locator_t& remote) const
{
	if (!IsLocatorSupported(remote))
	{
		return false;
	}

	Locator_t mainLocal(remote);
	//memset(mainLocal.address, 0x00, sizeof(mainLocal.address));
	mainLocal.set_Invalid_Address();
	return mainLocal;
}

bool SharedMemTransportInterface::transform_remote_locator(
		const Locator_t& remote_locator,
		Locator_t& result_locator) const
{
	if (IsLocatorSupported(remote_locator))
	{
		result_locator = remote_locator;

		return true;
	}

	return false;
}

bool SharedMemTransportInterface::send(
		const octet* send_buffer,
		uint32_t send_buffer_size,
		std::shared_ptr<eProsimaSharedMem::Writter> writer,
		const Locator_t& remote_locator,
		bool only_multicast_purpose,
		const std::chrono::microseconds& timeout)
{
	if (!IsLocatorSupported(remote_locator) /*|| send_buffer_size > configuration()->sendBufferSize*/)
	{
		return false;
	}
	
	try
	{
		writer->write(send_buffer, send_buffer_size, remote_locator, timeout);
	}
	catch (const std::exception& error)
	{
		logWarning(RTPS_MSG_OUT, error.what());
		return false;
	}

	/*logInfo(RTPS_MSG_OUT, "SharedMemTransport: " << send_buffer_size << " bytes TO endpoint: " << destinationEndpoint);
		<< " FROM " << getSocketPtr(socket)->local_endpoint());*/

	return true;
}

/**
 * Invalidate all selector entries containing certain multicast locator.
 *
 * This function will process all entries from 'index' onwards and, if any
 * of them has 'locator' on its multicast list, will invalidate them
 * (i.e. their 'transport_should_process' flag will be changed to false).
 *
 * If this function returns true, the locator received should be selected.
 *
 * @param entries   Selector entries collection to process
 * @param index     Starting index to process
 * @param locator   Locator to be searched
 *
 * @return true when at least one entry was invalidated, false otherwise
 */
static bool check_and_invalidate(
		fastrtps::ResourceLimitedVector<LocatorSelectorEntry*>& entries,
		size_t index,
		const Locator_t& locator)
{
	bool ret_val = false;
	for (; index < entries.size(); ++index)
	{
		LocatorSelectorEntry* entry = entries[index];
		if (entry->transport_should_process)
		{
			for (const Locator_t& loc : entry->multicast)
			{
				if (loc == locator)
				{
					entry->transport_should_process = false;
					ret_val = true;
					break;
				}
			}
		}
	}

	return ret_val;
}

void SharedMemTransportInterface::select_locators(
		LocatorSelector& selector) const
{
	fastrtps::ResourceLimitedVector<LocatorSelectorEntry*>& entries = selector.transport_starts();

	for (size_t i = 0; i < entries.size(); ++i)
	{
		LocatorSelectorEntry* entry = entries[i];
		if (entry->transport_should_process)
		{
			bool selected = false;

			// First try to find a multicast locator which is at least on another list.
			for (size_t j = 0; j < entry->multicast.size() && !selected; ++j)
			{
				if (IsLocatorSupported(entry->multicast[j]))
				{
					if (check_and_invalidate(entries, i + 1, entry->multicast[j]))
					{
						entry->state.multicast.push_back(j);
						selected = true;
					}
					else if (entry->unicast.size() == 0)
					{
						entry->state.multicast.push_back(j);
						selected = true;
					}
				}
			}

			// If we couldn't find a multicast locator, select all unicast locators
			if (!selected)
			{
				for (size_t j = 0; j < entry->unicast.size(); ++j)
				{
					if (IsLocatorSupported(entry->unicast[j].kind) && !selector.is_selected(entry->unicast[j]))
					{
						entry->state.unicast.push_back(j);
						selected = true;
					}
				}
			}

			// Select this entry if necessary
			if (selected)
			{
				selector.select(i);
			}
		}
	}
}

bool SharedMemTransportInterface::fillMetatrafficMulticastLocator(
		Locator_t& locator,
		uint32_t metatraffic_multicast_port) const
{
	if (locator.port == 0)
	{
		locator.port = metatraffic_multicast_port;
	}

	return true;
}

bool SharedMemTransportInterface::fillMetatrafficUnicastLocator(
		Locator_t& locator,
		uint32_t metatraffic_unicast_port) const
{
	if (locator.port == 0)
	{
		locator.port = metatraffic_unicast_port;
	}
	return true;
}

bool SharedMemTransportInterface::configureInitialPeerLocator(
		Locator_t& locator, 
		const PortParameters& port_params,
		uint32_t domainId, 
		LocatorList_t& list) const
{
	if (locator.port == 0)
	{
		for (uint32_t i = 0; i < configuration()->maxInitialPeersRange; ++i)
		{
			Locator_t auxloc(locator);
			auxloc.port = port_params.getUnicastPort(domainId, i);

			list.push_back(auxloc);
		}
	}
	else
		list.push_back(locator);

	return true;
}

bool SharedMemTransportInterface::fillUnicastLocator(
		Locator_t& locator, 
		uint32_t well_known_port) const
{
	if (locator.port == 0)
	{
		locator.port = well_known_port;
	}

	return true;
}

} // namespace rtps
} // namespace fastdds
} // namespace eprosima
