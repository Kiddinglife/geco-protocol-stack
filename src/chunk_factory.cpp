#include "chunk_factory.h"
#include "geco-malloc.h"
#include <cassert>

error_chunk_t* build_error_chunk()
{
    error_chunk_t* errorChunk = (error_chunk_t*) geco_malloc_ext(INIT_CHUNK_TOTAL_SIZE, __FILE__,
    __LINE__);
    if (errorChunk == NULL) ERRLOG(FALTAL_ERROR_EXIT, "malloc failed!\n");
    memset(errorChunk, 0, ERROR_CHUNK_TOTAL_SIZE);
    errorChunk->chunk_header.chunk_id = CHUNK_ERROR;
    errorChunk->chunk_header.chunk_flags = 0x00;
    errorChunk->chunk_header.chunk_length = CHUNK_FIXED_SIZE;
    return errorChunk;
}

uint put_ec_unrecognized_chunk(error_cause_t*ecause, uchar* errdata, uint errdatalen)
{
    assert(ecause != 0 && errdata != 0 && errdatalen > 0);

    ecause->error_reason_code = htons(VLPARAM_UNRECOGNIZED_PARAM);
    int len = errdatalen + ERR_CAUSE_FIXED_SIZE;
    ecause->error_reason_length = htons(len);
    if (errdatalen > 0 && errdata != NULL)
    {
        memcpy(ecause->error_reason, errdata, errdatalen);
    }
    while (len & 3)
        len++;
    return len;
}
uint put_error_cause(error_cause_t*ecause, ushort errcode, uchar* errdata, ushort errdatalen)
{
    assert(ecause != 0 && errdata != 0 && errdatalen > 0);

    ecause->error_reason_code = htons(errcode);
    int len = errdatalen + ERR_CAUSE_FIXED_SIZE;
    ecause->error_reason_length = htons(len);
    if (errdatalen > 0 && errdata != NULL) memcpy(ecause->error_reason, errdata, errdatalen);
    while (len & 3)
        len++;
    return len;
}
init_chunk_t* build_init_chunk(unsigned int initTag, unsigned int arwnd,
        unsigned short noOutStreams, unsigned short noInStreams, unsigned int initialTSN)
{
    init_chunk_t* initChunk = (init_chunk_t*) geco_malloc_ext(INIT_CHUNK_TOTAL_SIZE, __FILE__,
    __LINE__);
    if (initChunk == NULL) ERRLOG(FALTAL_ERROR_EXIT, "malloc failed!\n");
    memset(initChunk, 0, INIT_CHUNK_TOTAL_SIZE);
    initChunk->chunk_header.chunk_id = CHUNK_INIT;
    initChunk->chunk_header.chunk_flags = 0x00;
    initChunk->chunk_header.chunk_length = INIT_CHUNK_FIXED_SIZES;
    initChunk->init_fixed.init_tag = htonl(initTag);
    initChunk->init_fixed.rwnd = htonl(arwnd);
    initChunk->init_fixed.outbound_streams = htons(noOutStreams);
    initChunk->init_fixed.inbound_streams = htons(noInStreams);
    initChunk->init_fixed.initial_tsn = htonl(initialTSN);
    return initChunk;
}
void put_init_chunk_fixed(init_chunk_t* initChunk, unsigned int initTag, unsigned int arwnd,
        unsigned short noOutStreams, unsigned short noInStreams, unsigned int initialTSN)
{
    if (initChunk == NULL) ERRLOG(FALTAL_ERROR_EXIT, "malloc failed!\n");
    memset(initChunk, 0, INIT_CHUNK_TOTAL_SIZE);
    initChunk->chunk_header.chunk_id = CHUNK_INIT;
    initChunk->chunk_header.chunk_flags = 0x00;
    initChunk->chunk_header.chunk_length = INIT_CHUNK_FIXED_SIZES;
    initChunk->init_fixed.init_tag = htonl(initTag);
    initChunk->init_fixed.rwnd = htonl(arwnd);
    initChunk->init_fixed.outbound_streams = htons(noOutStreams);
    initChunk->init_fixed.inbound_streams = htons(noInStreams);
    initChunk->init_fixed.initial_tsn = htonl(initialTSN);
}
uint put_vlp_supported_addr_types(uchar* vlp_start, bool with_ipv4, bool with_ipv6, bool with_dns)
{
    ushort num_of_types = 0, position = 0;
    if (with_ipv4) num_of_types++;
    if (with_ipv6) num_of_types++;
    if (with_dns) num_of_types++;
    if (num_of_types == 0)
    ERRLOG(FALTAL_ERROR_EXIT,
            "put_supported_addr_types()::No Supported Address Types -- Program Error\n");
    ushort total_length = VLPARAM_FIXED_SIZE + num_of_types * sizeof(ushort);
    supported_address_types_t* param = (supported_address_types_t*) vlp_start;
    param->vlparam_header.param_type = htons(VLPARAM_SUPPORTED_ADDR_TYPES);
    param->vlparam_header.param_length = htons(total_length);
    if (with_ipv4)
    {
        param->address_type[position] = htons(VLPARAM_IPV4_ADDRESS);
        position++;
    }
    if (with_ipv6)
    {
        param->address_type[position] = htons(VLPARAM_IPV6_ADDRESS);
        position++;
    }
    if (with_dns)
    {
        param->address_type[position] = htons(VLPARAM_HOST_NAME_ADDR);
        position++;
    }
    /* take care of padding */
    if (total_length & 3) total_length += 2;
    return total_length;
}

uint put_vlp_addrlist(uchar* vlp_start, sockaddrunion local_addreslist[MAX_NUM_ADDRESSES],
        uint local_addreslist_size)
{
    assert(vlp_start != 0 && local_addreslist != 0 && local_addreslist_size > 0);

    uint i, length = 0;
    ip_address_t* ip_addr;
    for (i = 0; i < local_addreslist_size; i++)
    {

        ip_addr = (ip_address_t*) (vlp_start + length);
        switch (saddr_family(&(local_addreslist[i])))
        {
        case AF_INET:
            ip_addr->vlparam_header.param_type = htons(VLPARAM_IPV4_ADDRESS);
            ip_addr->vlparam_header.param_length = htons(
                    sizeof(struct in_addr) + VLPARAM_FIXED_SIZE);
            ip_addr->dest_addr_un.ipv4_addr = s4addr(&(local_addreslist[i]));
            assert(sizeof(struct in_addr) + VLPARAM_FIXED_SIZE == 8);
            length += 8;
            break;
        case AF_INET6:
            ip_addr->vlparam_header.param_type = htons(
            VLPARAM_IPV6_ADDRESS);
            ip_addr->vlparam_header.param_length = htons(
                    sizeof(struct in6_addr) + VLPARAM_FIXED_SIZE);
            memcpy(&ip_addr->dest_addr_un.ipv6_addr, &(s6addr(&(local_addreslist[i]))),
                    sizeof(struct in6_addr));
            assert(sizeof(struct in6_addr) + VLPARAM_FIXED_SIZE ==20);
            length += 20;
            break;
        default:
            ERRLOG1(MAJOR_ERROR,
                    "dispatch_layer_t::write_addrlist()::Unsupported Address Family %d",
                    saddr_family(&(local_addreslist[i])));
            break;
        }
    }
    return length;  // no need to align because MUST be 4 bytes aliged
}
