//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================


SETUP_LOG (WSDoor)

//
// This is a light weight, untrusted interface for web clients.
// For now we don't provide proof.  Later we will.
//
// Might need to support this header for browsers: Access-Control-Allow-Origin: *
// - https://developer.mozilla.org/en-US/docs/HTTP_access_control
//

//
// Strategy:
// - We only talk to NetworkOPs (so we will work even in thin mode)
// - NetworkOPs is smart enough to subscribe and or pass back messages
//
// VFALCO NOTE NetworkOPs isn't used here...
//

class WSDoorImp : public WSDoor, protected Thread, LeakChecked <WSDoorImp>
{
public:
    WSDoorImp (InfoSub::Source& source,
        std::string const& strIp, int iPort, bool bPublic,
            boost::asio::ssl::context& ssl_context)
        : WSDoor (source)
        , Thread ("websocket")
        , m_source (source)
        , m_ssl_context (ssl_context)
        , m_endpointLock (this, "WSDoor", __FILE__, __LINE__)
        , mPublic (bPublic)
        , mIp (strIp)
        , mPort (iPort)
    {
        startThread ();
    }

    ~WSDoorImp ()
    {
        stopThread ();
    }

private:
    void run ()
    {
        WriteLog (lsINFO, WSDoor) << boost::str (
            boost::format ("Websocket: %s: Listening: %s %d ") %
                (mPublic ? "Public" : "Private") % mIp % mPort);

        websocketpp::server_autotls::handler::ptr handler (
            new WSServerHandler <websocketpp::server_autotls> (m_source,
                m_ssl_context, mPublic));

        {
            ScopedLockType lock (m_endpointLock, __FILE__, __LINE__);

            m_endpoint = new websocketpp::server_autotls (handler);
        }

        // Call the main-event-loop of the websocket server.
        try
        {
            m_endpoint->listen (
                boost::asio::ip::tcp::endpoint (
                    boost::asio::ip::address ().from_string (mIp), mPort));
        }
        catch (websocketpp::exception& e)
        {
            WriteLog (lsWARNING, WSDoor) << "websocketpp exception: " << e.what ();

            // temporary workaround for websocketpp throwing exceptions on access/close races
            for (;;) 
            {
                // https://github.com/zaphoyd/websocketpp/issues/98
                try
                {
                    m_endpoint->get_io_service ().run ();
                    break;
                }
                catch (websocketpp::exception& e)
                {
                    WriteLog (lsWARNING, WSDoor) << "websocketpp exception: " << e.what ();
                }
            }
        }

        {
            ScopedLockType lock (m_endpointLock, __FILE__, __LINE__);

            m_endpoint = nullptr;
        }

        stopped ();
    }

    void onStop (Journal)
    {
        {
            ScopedLockType lock (m_endpointLock, __FILE__, __LINE__);

            // VFALCO NOTE we probably dont want to block here
            //             but websocketpp is deficient and broken.
            //
            if (m_endpoint != nullptr)
                m_endpoint->stop ();
        }

        signalThreadShouldExit ();
    }

private:
    typedef RippleRecursiveMutex LockType;
    typedef LockType::ScopedLockType ScopedLockType;

    InfoSub::Source& m_source;
    boost::asio::ssl::context& m_ssl_context;
    LockType m_endpointLock;

    ScopedPointer <websocketpp::server_autotls> m_endpoint;
    bool                            mPublic;
    std::string                     mIp;
    int                             mPort;
};

//------------------------------------------------------------------------------

WSDoor::WSDoor (Stoppable& parent)
    : Stoppable ("WSDoor", parent)
{
}

//------------------------------------------------------------------------------

WSDoor* WSDoor::New (InfoSub::Source& source, std::string const& strIp,
    int iPort, bool bPublic, boost::asio::ssl::context& ssl_context)
{
    ScopedPointer <WSDoor> door;

    try
    {
        door = new WSDoorImp (source, strIp, iPort, bPublic, ssl_context);
    }
    catch (...)
    {
        door = nullptr;
    }

    return door.release ();
}
