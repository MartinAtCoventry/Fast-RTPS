/*************************************************************************
 * Copyright (c) 2014 eProsima. All rights reserved.
 *
 * This copy of eProsima RTPS is licensed to you under the terms described in the
 * EPROSIMARTPS_LIBRARY_LICENSE file included in this distribution.
 *
 *************************************************************************/

/**
 * @file BuiltinProtocols.h
 *
 */

#ifndef BUILTINPROTOCOLS_H_
#define BUILTINPROTOCOLS_H_

#include "eprosimartps/dds/attributes/ParticipantAttributes.h"


namespace eprosima {
namespace rtps {

class PDPSimple;
class WLP;
class ParticipantImpl;
class RTPSWriter;
class RTPSReader;


class BuiltinProtocols {
public:
	BuiltinProtocols(ParticipantImpl* p_part);
	virtual ~BuiltinProtocols();

	bool initBuiltinProtocols(const DiscoveryAttributes& attributes, uint32_t participantID);
	bool updateMetatrafficLocators(LocatorList_t& loclist);

	DiscoveryAttributes m_attributes;
	ParticipantImpl* mp_participant;
	PDPSimple* mp_PDP;
	WLP* mp_WLP;
	uint32_t m_SPDP_WELL_KNOWN_MULTICAST_PORT;
	uint32_t m_SPDP_WELL_KNOWN_UNICAST_PORT;
	LocatorList_t m_metatrafficMulticastLocatorList;
	LocatorList_t m_metatrafficUnicastLocatorList;


	bool addLocalWriter(RTPSWriter* w);
	bool addLocalReader(RTPSReader* R);
	bool updateLocalWriter(RTPSWriter* W);
	bool updateLocalReader(RTPSReader* R);
	bool removeLocalWriter(RTPSWriter* W);
	bool removeLocalReader(RTPSReader* R);

	//! Announce ParticipantState (force the sending of a DPD message.)
	void announceParticipantState();
	//!Stop the Participant Announcement (used in tests to avoid multiple packets being send)
	void stopParticipantAnnouncement();
	//!Reset to timer to make periodic Participant Announcements.
	void resetParticipantAnnouncement();

};

} /* namespace rtps */
} /* namespace eprosima */

#endif /* BUILTINPROTOCOLS_H_ */
