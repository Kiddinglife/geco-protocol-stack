#include "dispatch_layer.h"
#include <algorithm>
#include "globals.h"

dispatch_layer_t::dispatch_layer_t()
{
    sctpLibraryInitialized = false;
    curr_channel_ = NULL;
    curr_geco_instance_ = NULL;
    last_source_addr_ = last_dest_addr_ = 0;
    last_src_port_ = last_dest_port_ = 0;
    last_init_tag_ = 0;
    last_src_path_ = 0;
    endpoints_list_.reserve(DEFAULT_ENDPOINT_SIZE);
}

void dispatch_layer_t::recv_dctp_packet(int socket_fd, char *dctp_packet,
    int dctp_packet_len, sockaddrunion * source_addr,
    sockaddrunion * dest_addr)
{
    event_logiii(verbose,
        "recv_dctp_packet()::recvied  %d bytes of data %s from dctp fd %d\n",
        dctp_packet_len, dctp_packet, socket_fd);

    /* 1) validate packet hdr size, checksum and if aligned 4 bytes */
    if (dctp_packet_len % 4 != 0
        || dctp_packet_len < (int)MIN_NETWORK_PACKET_HDR_SIZES
        || dctp_packet_len >(int) MAX_NETWORK_PACKET_HDR_SIZES
        || !validate_crc32_checksum(dctp_packet, dctp_packet_len))
    {
        event_log(loglvl_intevent, "received corrupted datagramm\n");
        //        last_source_addr_ = NULL;
        //        last_dest_addr_ = NULL;
        return;
    }

    /* 2) validate port numbers */
    dctp_packet_fixed_t* dctp_packet_fixed = (dctp_packet_fixed_t*)dctp_packet;
    last_src_port_ = ntohs(dctp_packet_fixed->src_port);
    last_dest_port_ = ntohs(dctp_packet_fixed->dest_port);
    if (last_src_port_ == 0 || last_dest_port_ == 0)
    {
        /* refers to RFC 4960 Section 3.1 at line 867 and line 874*/
        error_log(loglvl_minor_error,
            " dispatch_layer_t::recv_dctp_packet():: invalid ports number (0)\n");
        //        last_source_addr_ = NULL;
        //        last_dest_addr_ = NULL;
        last_src_port_ = 0;
        last_dest_port_ = 0;
        return;
    }

    /* 3) validate ip addresses */
    bool discard;
    int address_type;
    int supported_addr_types;
    uint ip4_saddr;
    switch (saddr_family(dest_addr))
    {
        case AF_INET:
            event_log(verbose,
                "dispatch_layer_t::recv_dctp_packet()::checking for correct IPV4 addresses\n");
            address_type = SUPPORT_ADDRESS_TYPE_IPV4;
            ip4_saddr = ntohl(dest_addr->sin.sin_addr.s_addr);
            if (IN_CLASSD(ip4_saddr))
                discard = true;
            if (IN_EXPERIMENTAL(ip4_saddr))
                discard = true;
            if (IN_BADCLASS(ip4_saddr))
                discard = true;
            if (INADDR_ANY == ip4_saddr)
                discard = true;
            if (INADDR_BROADCAST == ip4_saddr)
                discard = true;

            ip4_saddr = ntohl(source_addr->sin.sin_addr.s_addr);
            if (IN_CLASSD(ip4_saddr))
                discard = true;
            if (IN_EXPERIMENTAL(ip4_saddr))
                discard = true;
            if (IN_BADCLASS(ip4_saddr))
                discard = true;
            if (INADDR_ANY == ip4_saddr)
                discard = true;
            if (INADDR_BROADCAST == ip4_saddr)
                discard = true;

            /* we should not discard the msg sent to ourself */
            /* if ((INADDR_LOOPBACK != ntohl(source_addr->sin.sin_addr.s_addr)) &&
             (source_addr->sin.sin_addr.s_addr == dest_addr->sin.sin_addr.s_addr)) discard = true;*/
            break;

        case AF_INET6:
            event_log(verbose,
                "recv_dctp_packet: checking for correct IPV6 addresses\n");
            address_type = SUPPORT_ADDRESS_TYPE_IPV6;
#if defined (__linux__)
            if (IN6_IS_ADDR_UNSPECIFIED(dest_addr->sin6.sin6_addr.s6_addr))
                discard = true;
            if (IN6_IS_ADDR_MULTICAST(dest_addr->sin6.sin6_addr.s6_addr))
                discard = true;
            /* if (IN6_IS_ADDR_V4COMPAT(&(dest_addr->sin6.sin6_addr.s6_addr))) discard = true; */

            if (IN6_IS_ADDR_UNSPECIFIED(source_addr->sin6.sin6_addr.s6_addr))
                discard = true;
            if (IN6_IS_ADDR_MULTICAST(source_addr->sin6.sin6_addr.s6_addr))
                discard = true;
            /*  if (IN6_IS_ADDR_V4COMPAT(&(source_addr->sin6.sin6_addr.s6_addr))) discard = true; */
            /*
             if ((!IN6_IS_ADDR_LOOPBACK(&(source_addr->sin6.sin6_addr.s6_addr))) &&
             IN6_ARE_ADDR_EQUAL(&(source_addr->sin6.sin6_addr.s6_addr),
             &(dest_addr->sin6.sin6_addr.s6_addr))) discard = true;
             */
#else
            if (IN6_IS_ADDR_UNSPECIFIED(&dest_addr->sin6.sin6_addr)) discard = true;
            if (IN6_IS_ADDR_MULTICAST(&dest_addr->sin6.sin6_addr)) discard = true;
            /* if (IN6_IS_ADDR_V4COMPAT(&(dest_addr->sin6.sin6_addr))) discard = true; */

            if (IN6_IS_ADDR_UNSPECIFIED(&source_addr->sin6.sin6_addr)) discard = true;
            if (IN6_IS_ADDR_MULTICAST(&source_addr->sin6.sin6_addr)) discard = true;

            /* if (IN6_IS_ADDR_V4COMPAT(&(source_addr->sin6.sin6_addr))) discard = true; */
            /*
             if ((!IN6_IS_ADDR_LOOPBACK(&(source_addr->sin6.sin6_addr))) &&
             IN6_ARE_ADDR_EQUAL(&(source_addr->sin6.sin6_addr),
             &(dest_addr->sin6.sin6_addr))) discard = true;
             */
#endif
            break;

        default:
            error_log(loglvl_fatal_error_exit,
                "recv_dctp_packet()::Unsupported AddressType Received !\n");
            discard = true;
            break;
    }

    char src_addr_str[MAX_IPADDR_STR_LEN];
    saddr2str(source_addr, src_addr_str, MAX_IPADDR_STR_LEN, NULL);
    char dest_addr_str[MAX_IPADDR_STR_LEN];
    saddr2str(dest_addr, dest_addr_str, MAX_IPADDR_STR_LEN, NULL);
    event_logiiiii(loglvl_extevent,
        "recv_dctp_packet : packet_val_len %d, sourceaddress : %s, src_port %u,dest: %s, dest_port %u",
        dctp_packet_len, src_addr_str, last_src_port_, dest_addr_str,
        last_dest_port_);

    if (discard)
    {
        //        last_source_addr_ = NULL;
        //        last_dest_addr_ = NULL;
        last_src_port_ = 0;
        last_dest_port_ = 0;
        event_logii(loglvl_intevent,
            "recv_dctp_packet()::discarding packet for incorrect address\n src addr : %s,\ndest addr%s",
            src_addr_str, dest_addr_str);
        return;
    }

    /*4) find the endpoint for this packet */
    curr_channel_ = find_channel_by_transport_addr(last_source_addr_, last_src_port_,
        last_dest_port_);

    if (curr_channel_ != NULL)
    {
        /*5) found an sctp instance for this packet */
        curr_geco_instance_ = curr_channel_->dispatcher;
        supported_addr_types = 0;
    }
    else
    {
        /* 6)
         *  find dctp instancefor this packet
         *  if this packet is for a server dctp instance,
         *  we will find that dctp instance and let it handle this packet
         *  (i.e. we have the dctp instance's localPort set and
         *  it matches the packet's destination port) */
        curr_geco_instance_ = find_geco_instance_by_transport_addr(dest_addr, address_type);
        if (curr_geco_instance_ == NULL)
        {
            /* 7) may be an an endpoint that is a client (with instance port 0) */
            event_logi(verbose,
                "Couldn't find SCTP Instance for Port %u and Address in List !",
                last_dest_port_);
            address_type == SUPPORT_ADDRESS_TYPE_IPV4 ?
                supported_addr_types = SUPPORT_ADDRESS_TYPE_IPV4 :
                supported_addr_types = SUPPORT_ADDRESS_TYPE_IPV4
                | SUPPORT_ADDRESS_TYPE_IPV6;
        }
        else
        {
            supported_addr_types = curr_geco_instance_->supportedAddressTypes;
            event_logii(verbose,
                "Found an SCTP Instance for Port %u and Address in the list, types: %d !",
                last_dest_port_, supported_addr_types);
        }
    }

    /*8) now we can validate if dest_addr in localaddress */
    if (!validate_dest_addr(dest_addr))
    {
        event_log(verbose,
            "recv_dctp_packet()::this packet is not for me, DISCARDING !!!");
        last_source_addr_ = NULL;
        last_dest_addr_ = NULL;
        last_src_port_ = 0;
        last_dest_port_ = 0;
        curr_geco_instance_ = NULL;
        curr_channel_ = NULL;
        return;
    }

    /*9) fetch all chunk types contained in this packet value field for use in the folowing */
    int packet_value_len = dctp_packet_len - DCTP_PACKET_FIXED_SIZE;
    uint chunk_types = find_chunk_types(((dctp_packet_t*)dctp_packet)->chunk, packet_value_len);

    /*10) this packet contains init chunk and init-ack chunk that we need process on */
    if (curr_channel_ == NULL)
    {

    }


    last_init_tag_ = ntohl(dctp_packet_fixed->verification_tag);
    init_chunk_fixed_t* init_chunk_fixed;
    vlparam_fixed_t* vlparam_fixed;
    uchar* init_ptr;
    sockaddrunion alternate_addr;
    geco_instance_t temp_dispatcher;
}

uint dispatch_layer_t::find_chunk_types(uchar* packet_value, int packet_val_len)
{
    // 0000 0000 ret = 0 at beginning
    // 0000 0001 1
    // 1                chunktype init
    // 0000 0010 ret
    // 2                chunktype init ack 
    // 0000 0110 ret
    // 7                chunktype shutdown 
    // 1000 0110 ret
    // 192            chunktype shutdown 
    // 1000 0000-byte0-byte0-1000 0110 ret

    uint result = 0;
    int chunk_len = 0;
    int read_len = 0;
    uint padding_len;
    chunk_fixed_t* chunk;
    uchar* curr_pos = packet_value;

    while (read_len < packet_val_len)
    {
        event_logii(verbose,
            "find_chunk_types()::packet_val_len=%d, read_len=%d",
            packet_val_len, read_len);

        if (packet_val_len - read_len < CHUNK_FIXED_SIZE)
            event_log(loglvl_fatal_error_exit,
            "remainning bytes not enough for CHUNK_FIXED_SIZE(4 bytes) invalid !\n");

        chunk = (chunk_fixed_t*)curr_pos;
        chunk_len = get_chunk_length(chunk);
        if (chunk_len < CHUNK_FIXED_SIZE || chunk_len + read_len > packet_val_len)
            return result;

        if (chunk->chunk_id <= 30)
        {
            result |= (1 << chunk->chunk_id);
            event_logii(verbose,
                "dispatch_layer_t::find_chunk_types()::Chunk type==%u, result == %x",
                chunk->chunk_id, Bitify(sizeof(result) * 8, (char*)&result));
        }
        else
        {
            result |= (1 << 31);
            event_logii(verbose,
                "dispatch_layer_t::find_chunk_types()::Chunk type==%u setting bit 31 --> result == %s",
                chunk->chunk_id, Bitify(sizeof(result) * 8, (char*)&result));
        }

        read_len += chunk_len;
        padding_len = ((read_len % 4) == 0) ? 0 : (4 - read_len % 4);
        read_len += padding_len;
        curr_pos += read_len;
    }
}

bool dispatch_layer_t::cmp_geco_instance(const geco_instance_t& a,
    const geco_instance_t& b)
{
    event_logii(verbose,
        "DEBUG: cmp_geco_instance()::comparing instance a port %u, instance b port %u",
        a.local_port, b.local_port);

    if (a.local_port < b.local_port)
        return false;
    if (a.local_port > b.local_port)
        return false;

    if (!a.is_in6addr_any && !b.is_in6addr_any && a.is_inaddr_any
        && b.is_inaddr_any)
    {
        int i, j;
        /*find if at least there is an ip addr thate quals*/
        for (i = 0; i < a.local_addres_size; i++)
        {
            for (j = 0; j < b.local_addres_size; j++)
            {
                if (saddr_equals(&(a.local_addres_list[i]),
                    &(b.local_addres_list[j])))
                {
                    event_log(verbose,
                        "find_dispatcher_by_port(): found TWO equal dispatchers !");
                    return true;
                }
            }
        }
        return false;
    }
    else
    {
        /* one has IN(6)ADDR_ANY : return equal ! */
        return true;
    }
}

geco_instance_t* dispatch_layer_t::find_geco_instance_by_transport_addr(
    sockaddrunion* dest_addr, uint address_type)
{
    /* search for this endpoint from list*/
    tmp_dispather_.local_port = last_dest_port_;
    tmp_dispather_.local_addres_size = 1;
    tmp_dispather_.is_in6addr_any = false;
    tmp_dispather_.is_inaddr_any = false;
    tmp_dispather_.local_addres_list = dest_addr;
    tmp_dispather_.supportedAddressTypes = address_type;

    geco_instance_t* result = NULL;
    for (auto i = dispathers_list_.begin(); i != dispathers_list_.end(); i++)
    {
        if (cmp_geco_instance(tmp_dispather_, *(*i)))
        {
            result = *i;
            break;
        }
    }
    return result;
}

channel_t* dispatch_layer_t::find_channel_by_transport_addr(sockaddrunion * src_addr,
    ushort src_port, ushort dest_port)
{
    tmp_endpoint_.remote_addres_size = 1;
    tmp_endpoint_.remote_addres = &tmp_addr_;

    switch (saddr_family(src_addr))
    {
        case AF_INET:
            event_logi(loglvl_intevent, "Looking for IPv4 Address %x (in NBO)",
                s4addr(src_addr));
            tmp_endpoint_.remote_addres[0].sa.sa_family = AF_INET;
            tmp_endpoint_.remote_addres[0].sin.sin_addr.s_addr = s4addr(src_addr);
            tmp_endpoint_.remote_port = src_port;
            tmp_endpoint_.local_port = dest_port;
            tmp_endpoint_.deleted = false;
            break;
        case AF_INET6:
            tmp_endpoint_.remote_addres[0].sa.sa_family = AF_INET6;
            memcpy(&(tmp_endpoint_.remote_addres[0].sin6.sin6_addr.s6_addr),
                (s6addr(src_addr)), sizeof(struct in6_addr));
            event_logi(loglvl_intevent,
                "Looking for IPv6 Address %x, check NTOHX() ! ",
                tmp_endpoint_.remote_addres[0].sin6.sin6_addr.s6_addr);
            tmp_endpoint_.remote_port = src_port;
            tmp_endpoint_.local_port = dest_port;
            tmp_endpoint_.deleted = false;
            break;
        default:
            error_logi(loglvl_fatal_error_exit,
                "Unsupported Address Family %d in find_channel_by_transport_addr()",
                saddr_family(src_addr));
            break;
    }

    /* search for this endpoint from list*/
    channel_t* result = NULL;
    for (auto i = endpoints_list_.begin(); i != endpoints_list_.end(); i++)
    {
        if (cmp_channel(tmp_endpoint_, *(*i)))
        {
            result = *i;
            break;
        }
    }

    if (result != NULL)
    {
        if (result->deleted)
        {
            event_logi(verbose,
                "Found endpoint that should be deleted, with id %u\n",
                result->channel_id);
            result = NULL;
        }
        else
        {
            event_logi(verbose, "Found valid endpoint with id %u\n",
                result->channel_id);
        }
    }
    else
    {
        event_log(loglvl_intevent,
            "endpoint indexed by transport address not in list");
    }

    return result;
}

bool dispatch_layer_t::cmp_channel(const channel_t& a,
    const channel_t& b)
{
    event_logii(verbose,
        "cmp_endpoint_by_addr_port(): checking ep A[id=%d] and ep B[id=%d]\n",
        a.channel_id, b.channel_id);
    if (a.remote_port == b.remote_port && a.local_port == b.local_port)
    {
        uint i, j;
        /*find if at least there is an ip addr thate quals*/
        for (i = 0; i < a.remote_addres_size; i++)
        {
            for (j = 0; j < b.remote_addres_size; j++)
            {
                if (saddr_equals(&(a.remote_addres[i]), &(b.remote_addres[j])))
                {
                    if (!a.deleted && !b.deleted)
                    {
                        event_log(verbose,
                            "cmp_endpoint_by_addr_port(): found TWO equal ep !");
                        return true;
                    }
                }
            }
        }
        event_log(verbose, "cmp_endpoint_by_addr_port(): found No equal ep !");
        return false;
    }
    else
    {
        event_log(verbose, "cmp_endpoint_by_addr_port(): found No equal ep !");
        return false;
    }
}

bool dispatch_layer_t::validate_dest_addr(sockaddrunion * dest_addr)
{
    /* 1)
     * this case will be specially treated
     * after the call to validate_dest_addr()
     * when curr_channel_ not null, curr_geco_instance_ MUST NOT be null*/
    if (curr_geco_instance_ == NULL && curr_channel_ == NULL)
        return true;

    uint j;
    if (curr_channel_ != NULL)
    {
        /* 2) check if it is in curr_channel_'s local addresses list*/
        for (j = 0; j < curr_channel_->local_addres_size; j++)
        {
            if (saddr_equals(&curr_channel_->local_addres[j], dest_addr))
            {
                event_logii(verbose,
                    "dispatch_layer_t::validate_dest_addr()::Checking dest addr  %x, local %x",
                    s4addr(dest_addr),
                    s4addr(&(curr_channel_->local_addres[j])));
                return true;
            }
        }
    }

    ushort af = saddr_family(dest_addr);
    bool any_set = false;

    /* 3) check whether _instance_ has INADDR_ANY
     * curr_geco_instance_ MUST NOT be null at the moment */
    if (curr_geco_instance_->is_inaddr_any)
    {
        any_set = true;

        if (af == AF_INET)
            return true;

        if (af == AF_INET6)
            return false;
    }

    if (curr_geco_instance_->is_in6addr_any)
    {
        any_set = true;

        if (af == AF_INET || af == AF_INET6)
            return true;
    }

    if (any_set)
        return false;

    /* 4) search through local address list of this dctp instance */
    for (j = 0; j < curr_geco_instance_->local_addres_size; j++)
    {
        if (saddr_equals(dest_addr, &(curr_geco_instance_->local_addres_list[j])))
        {
            return true;
        }
    }

    return false;
}