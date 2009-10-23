// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2009 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */


#include <errno.h>

#include "CephxClientHandler.h"
#include "CephxProtocol.h"

#include "../KeyRing.h"

int CephxClientHandler::build_request(bufferlist& bl)
{
  dout(0) << "state=" << state << dendl;

  switch (state) {
  case STATE_START:
    break;

  case STATE_GETTING_MON_KEY:
    /* authenticate */
    {
      /* FIXME: init req fields */
      CephXGetMonKey req;
      req.name = client->name;
      CryptoKey secret;
      g_keyring.get_master(secret);
      bufferlist key, key_enc;
      get_random_bytes((char *)&req.client_challenge, sizeof(req.client_challenge));
      ::encode(server_challenge, key);
      ::encode(req.client_challenge, key);
      int ret = encode_encrypt(key, secret, key_enc);
      if (ret < 0)
        return ret;
      req.key = 0;
      const uint64_t *p = (const uint64_t *)key_enc.c_str();
      for (int pos = 0; pos + sizeof(req.key) <= key_enc.length(); pos+=sizeof(req.key), p++) {
        req.key ^= *p;
      }
      ::encode(req, bl);

      /* we first need to get the principle/auth session key */
      CephXRequestHeader header;
      header.request_type = CEPHX_GET_AUTH_SESSION_KEY;
      ::encode(header, bl);
      build_authenticate_request(client->name, bl);
      return 0;
    }
    break;

  case STATE_GETTING_SESSION_KEYS:
    /* get service tickets */
    {
      dout(0) << "want=" << hex << client->want << " have=" << client->have << dec << dendl;

      AuthTicketHandler& ticket_handler = client->tickets.get_handler(CEPH_ENTITY_TYPE_AUTH);
      if (!ticket_handler.build_authorizer(authorizer))
	return -EINVAL;

      CephXRequestHeader header;
      header.request_type = CEPHX_GET_PRINCIPAL_SESSION_KEY;
      ::encode(header, bl);
      
      bl.claim_append(authorizer.bl);
      build_service_ticket_request(client->want, bl);
    }
    break;

  case STATE_DONE:
    break;

  default:
    assert(0);
  }
  return 0;
}

int CephxClientHandler::handle_response(int ret, bufferlist::iterator& indata)
{
  dout(0) << "cephx handle_response ret = " << ret << " state " << state << dendl;

  switch (state) {
  case STATE_START:
    /* initialize  */
    { 
      CephXEnvResponse1 response;
      ::decode(response, indata);
      server_challenge = response.server_challenge;
      state = STATE_GETTING_MON_KEY;
      ret = -EAGAIN;
    }
    break;
  case STATE_GETTING_MON_KEY:
    /* authenticate */
    {
      struct CephXResponseHeader header;
      ::decode(header, indata);

      dout(0) << "request_type=" << hex << header.request_type << dec << dendl;
      dout(0) << "handle_cephx_response()" << dendl;

      dout(0) << "CEPHX_GET_AUTH_SESSION_KEY" << dendl;
      
      CryptoKey secret;
      g_keyring.get_master(secret);
	
      if (!client->tickets.verify_service_ticket_reply(secret, indata)) {
	dout(0) << "could not verify service_ticket reply" << dendl;
	return -EPERM;
      }
      dout(0) << "want=" << client->want << " have=" << client->have << dendl;
      if (client->want != client->have) {
	state = STATE_GETTING_SESSION_KEYS;
	ret = -EAGAIN;
      } else {
	state = STATE_DONE;
	ret = 0;
      }
    }
    break;

  case STATE_GETTING_SESSION_KEYS:
    {
      struct CephXResponseHeader header;
      ::decode(header, indata);

      AuthTicketHandler& ticket_handler = client->tickets.get_handler(CEPH_ENTITY_TYPE_AUTH);
      dout(0) << "CEPHX_GET_PRINCIPAL_SESSION_KEY session_key " << ticket_handler.session_key << dendl;
  
      if (!client->tickets.verify_service_ticket_reply(ticket_handler.session_key, indata)) {
        dout(0) << "could not verify service_ticket reply" << dendl;
        return -EPERM;
      }
      if (client->want == client->have) {
	state = STATE_DONE;
	ret = 0;
      }
    }
    break;

  case STATE_DONE:
    // ignore?
    ret = 0;
    break;

  default:
    assert(0);
  }
  return ret;
}



/*


int AuthClientAuthorizeHandler::_build_request()
{
  CephXRequestHeader header;
  if (!client->tickets.has_key(service_id)) {
    dout(0) << "can't authorize: missing service key" << dendl;
    return -EPERM;
  }

  header.request_type = CEPHX_OPEN_SESSION;

  MAuthorize *m = (MAuthorize *)msg;
  bufferlist& bl = m->get_auth_payload();

  ::encode(header, bl);
  utime_t now;

  if (!client->tickets.build_authorizer(service_id, authorizer))
    return -EINVAL;

  bl.claim_append(authorizer.bl);

  return 0;
}

int AuthClientAuthorizeHandler::_handle_response(int ret, bufferlist::iterator& iter)
{
  struct CephXResponseHeader header;
  ::decode(header, iter);

  dout(0) << "AuthClientAuthorizeHandler::_handle_response() ret=" << ret << dendl;

  if (ret) {
    return ret;
  }

  switch (header.request_type & CEPHX_REQUEST_TYPE_MASK) {
  case CEPHX_OPEN_SESSION:
    {
      ret = authorizer.verify_reply(iter);
      break;
    }
    break;
  default:
    dout(0) << "header.request_type = " << hex << header.request_type << dec << dendl;
    ret = -EINVAL;
    break;
  }

  return ret;
}

AuthClientProtocolHandler *AuthClientHandler::_get_proto_handler(uint32_t id)
{
  map<uint32_t, AuthClientProtocolHandler *>::iterator iter = handlers_map.find(id);
  if (iter == handlers_map.end())
    return NULL;

  return iter->second;
}


*/
