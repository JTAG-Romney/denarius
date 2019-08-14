// Copyright (c) 2012-2013 giv
// Copyright (c) 2019 Denarius developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//--------------------------------------------------------------------------------------------------
#include <boost/thread/shared_mutex.hpp>
#include <openssl/sha.h>
#include <iostream>
#include "i2p.h"
#include "util.h"
#include "uint256.h"
#include "hash.h"
#include "netbase.h"

char I2PKeydat [1024] = {'\0'};

namespace SAM
{

class StreamSessionAdapter::SessionHolder
{
public:
	explicit SessionHolder(std::shared_ptr<SAM::StreamSession> session);
	~SessionHolder();

	const SAM::StreamSession& getSession() const;
	SAM::StreamSession& getSession();
private:
	void heal() const;
	void reborn() const;

	mutable std::shared_ptr<SAM::StreamSession> session_;
	typedef boost::shared_mutex mutex_type;
	mutable mutex_type mtx_;
};

StreamSessionAdapter::SessionHolder::SessionHolder(std::shared_ptr<SAM::StreamSession> session)
	: session_(session)
{
}

StreamSessionAdapter::SessionHolder::~SessionHolder()
{
}

const SAM::StreamSession& StreamSessionAdapter::SessionHolder::getSession() const
{
	boost::upgrade_lock<mutex_type> lock(mtx_);
	if (session_->isSick())
	{
		boost::upgrade_to_unique_lock<mutex_type> ulock(lock);
		heal();
	}
	return *session_;
}

SAM::StreamSession& StreamSessionAdapter::SessionHolder::getSession()
{
	boost::upgrade_lock<mutex_type> lock(mtx_);
	if (session_->isSick())
	{
		boost::upgrade_to_unique_lock<mutex_type> ulock(lock);
		heal();
	}
	return *session_;
}

void StreamSessionAdapter::SessionHolder::heal() const
{
	reborn(); // if we don't know how to heal it just reborn it
}

void StreamSessionAdapter::SessionHolder::reborn() const
{
	if (!session_->isSick())
		return;
	std::shared_ptr<SAM::StreamSession> newSession(new SAM::StreamSession(*session_));
	if (!newSession->isSick() && session_->isSick())
		session_ = newSession;
}

//--------------------------------------------------------------------------------------------------

StreamSessionAdapter::StreamSessionAdapter()
{
	SAM::StreamSession::SetLogFile ((GetDataDir() / "i2pdebug.log").string ());
}

StreamSessionAdapter::~StreamSessionAdapter()
{
	SAM::StreamSession::CloseLogFile ();
}

bool StreamSessionAdapter::StartSession (
		const std::string& nickname,
		const std::string& SAMHost       /*= SAM_DEFAULT_ADDRESS*/,
		uint16_t SAMPort                 /*= SAM_DEFAULT_PORT*/,
		const std::string& myDestination /*= SAM_GENERATE_MY_DESTINATION*/,
		const std::string& i2pOptions    /*= SAM_DEFAULT_I2P_OPTIONS*/,
		const std::string& minVer        /*= SAM_DEFAULT_MIN_VER*/,
		const std::string& maxVer        /*= SAM_DEFAULT_MAX_VER*/)
{
	std::cout << "Creating Denarius I2P SAM session..." << std::endl;
	auto s = std::make_shared<SAM::StreamSession>(nickname, SAMHost, SAMPort, myDestination, i2pOptions, minVer, maxVer);
	sessionHolder_ = std::make_shared<SessionHolder>(s);
	bool isReady = s->isReady ();
	if (isReady)
		std::cout << "Denarius I2P SAM session created" << std::endl;
	else
		std::cout << "Denarius I2P SAM session failed" << std::endl;
	return isReady;
}

void StreamSessionAdapter::StopSession ()
{
	std::cout << "Terminating I2P SAM session..." << std::endl;
	sessionHolder_ = nullptr;
	std::cout << "Denarius I2P SAM session terminated" << std::endl;
}

bool StreamSessionAdapter::Start ()
{
	return StartSession(
		GetArg(I2P_SESSION_NAME_PARAM, I2P_SESSION_NAME_DEFAULT),
		GetArg(I2P_SAM_HOST_PARAM, I2P_SAM_HOST_DEFAULT),
		(uint16_t)GetArg(I2P_SAM_PORT_PARAM, I2P_SAM_PORT_DEFAULT),
		GetArg(I2P_SAM_MY_DESTINATION_PARAM, I2P_SAM_MY_DESTINATION_DEFAULT),
		GetArg(I2P_SAM_I2P_OPTIONS_PARAM, SAM_DEFAULT_I2P_OPTIONS));
}

void StreamSessionAdapter::Stop ()
{
	StopSession ();
}

bool StreamSessionAdapter::isSick( void ) const
{
	SAM::StreamSession& s = sessionHolder_->getSession();
	// ToDo: Find out why it's sick and perhaps restart the session later, more than likely the socket would not open
	return s.isSick();
}

SAM::SOCKET StreamSessionAdapter::accept(bool silent)
{
	SAM::RequestResult<std::shared_ptr<SAM::Socket> > result = sessionHolder_->getSession().accept(silent);
	// call Socket::release
	return result.isOk ? result.value->release() : SAM_INVALID_SOCKET;
}

SAM::SOCKET StreamSessionAdapter::connect(const std::string& destination, bool silent)
{
	SAM::RequestResult<std::shared_ptr<SAM::Socket> > result = sessionHolder_->getSession().connect(destination, silent);
	// call Socket::release
	return result.isOk ? result.value->release() : SAM_INVALID_SOCKET;
}

bool StreamSessionAdapter::forward(const std::string& host, uint16_t port, bool silent)
{
	return sessionHolder_->getSession().forward(host, port, silent).isOk;
}

std::string StreamSessionAdapter::namingLookup(const std::string& name) const
{
	SAM::RequestResult<const std::string> result = sessionHolder_->getSession().namingLookup(name);
	return result.isOk ? result.value : std::string();
}

SAM::FullDestination StreamSessionAdapter::destGenerate() const
{
	SAM::RequestResult<const SAM::FullDestination> result = sessionHolder_->getSession().destGenerate();
	return result.isOk ? result.value : SAM::FullDestination();
}

void StreamSessionAdapter::stopForwarding(const std::string& host, uint16_t port)
{
	sessionHolder_->getSession().stopForwarding(host, port);
}

void StreamSessionAdapter::stopForwardingAll()
{
	sessionHolder_->getSession().stopForwardingAll();
}

const SAM::FullDestination& StreamSessionAdapter::getMyDestination() const
{
	return sessionHolder_->getSession().getMyDestination();
}

const sockaddr_in& StreamSessionAdapter::getSAMAddress() const
{
	return sessionHolder_->getSession().getSAMAddress();
}

const std::string& StreamSessionAdapter::getSAMHost() const
{
	return sessionHolder_->getSession().getSAMHost();
}

uint16_t StreamSessionAdapter::getSAMPort() const
{
	return sessionHolder_->getSession().getSAMPort();
}

const std::string& StreamSessionAdapter::getNickname() const
{
	return sessionHolder_->getSession().getNickname();
}

const std::string& StreamSessionAdapter::getSAMMinVer() const
{
	return sessionHolder_->getSession().getSAMMinVer();
}

const std::string& StreamSessionAdapter::getSAMMaxVer() const
{
	return sessionHolder_->getSession().getSAMMaxVer();
}

const std::string& StreamSessionAdapter::getSAMVersion() const
{
	return sessionHolder_->getSession().getSAMVersion();
}

const std::string& StreamSessionAdapter::getOptions() const
{
	return sessionHolder_->getSession().getOptions();
}

const std::string& StreamSessionAdapter::getSessionID() const
{
	return sessionHolder_->getSession().getSessionID();
}

} // namespace SAM

//--------------------------------------------------------------------------------------------------
static std::string sSession = I2P_SESSION_NAME_DEFAULT;
static std::string sHost = SAM_DEFAULT_ADDRESS;
static uint16_t uPort = SAM_DEFAULT_PORT;
static std::string sDestination = SAM_GENERATE_MY_DESTINATION;
static std::string sOptions;

I2PSession::I2PSession()
{}

I2PSession::~I2PSession()
{}

void static FormatBoolI2POptionsString( std::string& I2pOptions, const std::string& I2pSamName, const std::string& confParamName )
{
    bool fConfigValue = GetArg( confParamName, true );

    if (!I2pOptions.empty()) I2pOptions += " ";                             // seperate the parameters with <whitespace>
    I2pOptions += I2pSamName + "=" + (fConfigValue ? "true" : "false");     // I2P router wants the words...
}

void static FormatIntI2POptionsString( std::string& I2pOptions, const std::string& I2pSamName, const std::string& confParamName )
{
    int64_t i64ConfigValue = GetArg( confParamName, 0 );

    if (!I2pOptions.empty()) I2pOptions += " ";                     // seperate the parameters with <whitespace>
    std::ostringstream oss;
    oss << i64ConfigValue;                                          // One way of converting a value to a string
    I2pOptions += I2pSamName + "=" + oss.str();                     // I2P router wants the chars....
}

void static BuildI2pOptionsString( void )
{
    std::string OptStr;

    FormatIntI2POptionsString(OptStr, SAM_NAME_INBOUND_QUANTITY       , "-i2p.options.inbound.quantity" );
    FormatIntI2POptionsString(OptStr, SAM_NAME_INBOUND_LENGTH         , "-i2p.options.inbound.length" );
    FormatIntI2POptionsString(OptStr, SAM_NAME_INBOUND_LENGTHVARIANCE , "-i2p.options.inbound.lengthvariance" );
    FormatIntI2POptionsString(OptStr, SAM_NAME_INBOUND_BACKUPQUANTITY , "-i2p.options.inbound.backupquantity" );
    FormatBoolI2POptionsString(OptStr, SAM_NAME_INBOUND_ALLOWZEROHOP  , "-i2p.options.inbound.allowzerohop" );
    FormatIntI2POptionsString(OptStr, SAM_NAME_INBOUND_IPRESTRICTION  , "-i2p.options.inbound.iprestriction" );
    FormatIntI2POptionsString(OptStr, SAM_NAME_OUTBOUND_QUANTITY      , "-i2p.options.outbound.quantity" );
    FormatIntI2POptionsString(OptStr, SAM_NAME_OUTBOUND_LENGTH        , "-i2p.options.outbound.length" );
    FormatIntI2POptionsString(OptStr, SAM_NAME_OUTBOUND_LENGTHVARIANCE, "-i2p.options.outbound.lengthvariance" );
    FormatIntI2POptionsString(OptStr, SAM_NAME_OUTBOUND_BACKUPQUANTITY, "-i2p.options.outbound.backupquantity" );
    FormatBoolI2POptionsString(OptStr, SAM_NAME_OUTBOUND_ALLOWZEROHOP , "-i2p.options.outbound.allowzerohop" );
    FormatIntI2POptionsString(OptStr, SAM_NAME_OUTBOUND_IPRESTRICTION , "-i2p.options.outbound.iprestriction" );
    FormatIntI2POptionsString(OptStr, SAM_NAME_OUTBOUND_PRIORITY      , "-i2p.options.outbound.priority" );

    std::string ExtrasStr = GetArg( "-i2p.options.extra", "");
    if( ExtrasStr.size() ) {
        if (!OptStr.empty()) OptStr += " ";                             // seperate the parameters with <whitespace>
        OptStr += ExtrasStr;
    }
    sOptions = OptStr;                                                  // Keep this globally for use later in opening the session
}

// Initialize all the parameters with default values, if necessary.
// These should not override any loaded from the denarius.conf file,
// but if they are not set, then this insures that they are created with good values
// SoftSetArg will return a bool, if you care to know if the parameter was changed or not.
void InitializeI2pSettings( const bool fGenerated )
{
    if( SoftSetArg( "-i2p.options.samhost", SAM_DEFAULT_ADDRESS ) )    // Returns true if the param was undefined and setting its value was possible
        LogPrintf( "i2psettings() : parameter interaction: -i2p.options.enabled -> setting -i2p.options.samhost=%s\n", SAM_DEFAULT_ADDRESS );
    if( SoftSetArg( "-i2p.options.samport", "7656" ) )
        LogPrintf( "i2psettings() : parameter interaction: -i2p.options.enabled -> setting -i2p.options.samport=%d\n", SAM_DEFAULT_PORT );
    if( SoftSetArg( "-i2p.options.sessionname", I2P_SESSION_NAME_DEFAULT ) )
        LogPrintf( "i2psettings() : parameter interaction: -i2p.options.enabled -> setting -i2p.options.sessionname=%s\n", I2P_SESSION_NAME_DEFAULT );
    if( SoftSetArg( "-i2p.options.inbound.quantity", "3" ) )
        LogPrintf( "i2psettings() : parameter interaction: -i2p.options.enabled -> setting -i2p.options.inbound.quantity=%d\n", SAM_DEFAULT_INBOUND_QUANTITY );
    if( SoftSetArg( "-i2p.options.inbound.length", "3" ) )
        LogPrintf( "i2psettings() : parameter interaction: -i2p.options.enabled -> setting -i2p.options.inbound.length=%d\n", SAM_DEFAULT_INBOUND_LENGTH );
    if( SoftSetArg( "-i2p.options.inbound.lengthvariance", "0" ) )
        LogPrintf( "i2psettings() : parameter interaction: -i2p.options.enabled -> setting -i2p.options.inbound.lengthvariance=%d\n", SAM_DEFAULT_INBOUND_LENGTHVARIANCE );
    if( SoftSetArg( "-i2p.options.inbound.backupquantity", "1" ) )
        LogPrintf( "i2psettings() : parameter interaction: -i2p.options.enabled -> setting -i2p.options.inbound.backupquantity=%d\n", SAM_DEFAULT_INBOUND_BACKUPQUANTITY );
    if( SoftSetBoolArg( "-i2p.options.inbound.allowzerohop", SAM_DEFAULT_INBOUND_ALLOWZEROHOP ) )
        LogPrintf( "i2psettings() : parameter interaction: -i2p.options.enabled -> setting -i2p.options.inbound.allowzerohop=%d\n", SAM_DEFAULT_OUTBOUND_ALLOWZEROHOP ? 1 : 0 );
    if( SoftSetArg( "-i2p.options.inbound.iprestriction", "2" ) )
        LogPrintf( "i2psettings() : parameter interaction: -i2p.options.enabled -> setting -i2p.options.inbound.iprestriction=%d\n", SAM_DEFAULT_INBOUND_IPRESTRICTION );
    if( SoftSetArg( "-i2p.options.outbound.quantity", "3" ) )
        LogPrintf( "i2psettings() : parameter interaction: -i2p.options.enabled -> setting -i2p.options.outbound.quantity=%d\n", SAM_DEFAULT_OUTBOUND_QUANTITY );
    if( SoftSetArg( "-i2p.options.outbound.length", "3" ) )
        LogPrintf( "i2psettings() : parameter interaction: -i2p.options.enabled -> setting -i2p.options.outbound.length=%d\n", SAM_DEFAULT_OUTBOUND_LENGTH );
    if( SoftSetArg( "-i2p.options.outbound.lengthvariance", "0" ) )
        LogPrintf( "i2psettings() : parameter interaction: -i2p.options.enabled -> setting -i2p.options.outbound.lengthvariance=%d\n", SAM_DEFAULT_OUTBOUND_LENGTHVARIANCE );
    if( SoftSetArg( "-i2p.options.outbound.backupquantity", "1" ) )
        LogPrintf( "i2psettings() : parameter interaction: -i2p.options.enabled -> setting -i2p.options.outbound.backupquantity=%d\n", SAM_DEFAULT_OUTBOUND_BACKUPQUANTITY );
    if( SoftSetBoolArg( "-i2p.options.outbound.allowzerohop", SAM_DEFAULT_OUTBOUND_ALLOWZEROHOP ) )
        LogPrintf( "i2psettings() : parameter interaction: -i2p.options.enabled -> setting -i2p.options.outbound.allowzerohop=%d\n", SAM_DEFAULT_OUTBOUND_ALLOWZEROHOP ? 1 : 0 );
    if( SoftSetArg( "-i2p.options.outbound.iprestriction", "2" ) )
        LogPrintf( "i2psettings() : parameter interaction: -i2p.options.enabled -> setting -i2p.options.outbound.iprestriction=%d\n", SAM_DEFAULT_OUTBOUND_IPRESTRICTION );
    if( SoftSetArg( "-i2p.options.outbound.priority", "0" ) )
        LogPrintf( "i2psettings() : parameter interaction: -i2p.options.enabled -> setting -i2p.options.outbound.priority=%d\n", SAM_DEFAULT_OUTBOUND_PRIORITY );
    if( !SoftSetBoolArg("-i2p.options.static", false) )           // Returns true if the param was undefined and setting its value was possible
        LogPrintf( "i2psettings() : PLEASE REMOVE -i2p.options.static= FROM YOUR CONFIGURATION FILE - It has moved to the [i2p.mydestination] section.\n" );

    // Setup parameters required to open a new SAM session
    uPort = (uint16_t)GetArg( "-i2p.options.samport", SAM_DEFAULT_PORT );
    sSession = GetArg( "-i2p.options.sessionname", I2P_SESSION_NAME_DEFAULT );
    sHost = GetArg( "-i2p.options.samhost", SAM_DEFAULT_ADDRESS );
    // Critical to check here, if we are in dynamic destination mode, the intial session destination MUSTBE default too.
    //  Which may not be what the user has set in the denarius.conf file.
    // If the .static i2p destination is to be used, set it now, if not set the TRANSIENT value so the router generates it for us
    if (!I2PKeydat[0]) { // There is an empty I2PKeydat in memory, so no file I2Pkey.dat on disk OR mydestination privatekey in denarius.conf is set
        //LogPrintf("I2Pwrapper: we do not read from I2Pkey.dat\n"); 
        sDestination = fGenerated ? SAM_GENERATE_MY_DESTINATION : GetArg( "-i2p.mydestination.privatekey", "" ); // either static with privatekey in denarius.conf or DYN
    } else {
        LogPrintf("I2Pwrapper: SAM destination generated from file i2pkey.dat\n"); 
        sDestination = fGenerated ? SAM_GENERATE_MY_DESTINATION : I2PKeydat;
    }
//sDestination = fGenerated ? SAM_GENERATE_MY_DESTINATION : GetArg( "-i2p.mydestination.privatekey", "" );
    // It's important here that sDestination be setup correctly, for soon when an initial Session object is about to be
    // created.  When that happens, this variable is used to create the SAM session, upon which, after it's opened,
    // the variable is updated to reflect that ACTUAL destination being used.  Whatever that value maybe,
    // dynamically generated or statically set.  ToDo: Move sDestination into the Session class

    BuildI2pOptionsString();   // Now build the I2P options string that's need to open a session
}

//Newer I2P functions for b32
bool isValidI2pDestination( const SAM::FullDestination& DestKeys ) {

    // Perhaps we're given a I2P native public address, last 4 symbols of b64-destination must be AAAA
    bool fPublic = ((DestKeys.pub.size() == NATIVE_I2P_DESTINATION_SIZE) && isValidI2pAddress( DestKeys.pub));
    // ToDo: Add more checking on the private key, for now this will do...
    bool fPrivate = ((DestKeys.priv.size() > NATIVE_I2P_DESTINATION_SIZE) && isValidI2pAddress( DestKeys.priv));
    return fPublic && fPrivate;
}

std::string GetDestinationPublicKey( const std::string& sDestinationPrivateKey )
{
    return( sDestinationPrivateKey.substr(0, NATIVE_I2P_DESTINATION_SIZE) );
}

/*static*/
std::string I2PSession::GenerateB32AddressFromDestination(const std::string& destination)
{
    return ::B32AddressFromDestination(destination);
}

std::string GenerateI2pDestinationMessage( const std::string& pub, const std::string& priv, const std::string& b32, const std::string& configFileName )
{
    std::string msg;

    msg  = ("\nTo have a permanent I2P Destination address, you have to set options in denarius.conf:\n");
    msg += ("Your Config file is: ");
    msg += configFileName.c_str();

    msg += ("\nYour I2P Destination Private Key denarius.conf file settings are:\n\n");
    msg += strprintf( "[i2p.mydestination]\nstatic=1\nprivatekey=%s\n\n[i2p.options]\nenabled=1\n\n", priv.c_str() );
    msg += ("****** Save the above text at the end of your configuration file and keep it secret.\n");
    msg += ("       Or start with our denarius.conf.sample file, which has even more settings.\n");

    msg += ("\nThis is your I2P Public Key:\n");
    msg += pub.c_str();
    msg += ("\n\n**** You can advertise the Public Key, all I2P Routers will know how to locate it.\n");

    msg += ("\nYour personal b32.i2p Destination:\n");
    msg += b32.c_str();
    msg += ("\n\n** denarius peers now have built-in name resolution, once your on I2P for a few hours,\n");
    msg += ("   most peers will likely be able to find you by this Base32 hash of the Public Key.\n");
    msg += ("   Standard I2P network Routers are not as likely to find your destination with it.\n");
    // msg += "\n\n";

    return msg;
}
