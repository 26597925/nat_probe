/**
 * @file	nat_probe.h
 * @author  <wanghao1@xunlei.com>
 * @date	Nov  9 21:07:06 CST 2016
 *
 * @brief	̽��·������NAT���͵Ĺ���ͷ�ļ�
 *
 */

#ifndef __NAT_PROBE_H__
#define __NAT_PROBE_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// �������ͱ仯�¼�
#define NP_NETWORK_TYPE_CHANGE_EVENT	"nat_probe.network_type_change"

enum
{
	NP_UNKNOWN					= 0,	/**< δ֪��������. */
	NP_PUBLIC_NETWORK,					/**< ��������. */
	NP_FULL_CONE_NAT,					/**< ȫ׶��NAT. */
	NP_RESTRICTED_CONE_NAT,				/**< ������NAT. */
	NP_PORT_RESTRICTED_CONE_NAT,		/**< �˿�������NAT. */
	NP_SYMMETRIC_NAT,					/**< �Գ���NAT. */
};

/// ����������������.
#define NP_DOMAIN	"t05b022.p2cdn.com"

/// �����������Ķ˿�.
#define NP_SERVER_PORT	(61620)

/// ��NAT�����ж���Ҫ����IP�ĸ���.
#define NP_PUBLIC_IP_NUM	(2)

struct np_request_msg_t
{
	int			msgid;			/**< ��Ϣid. */
	int			network_type;	/**< ��������. */
};

struct np_response_msg_t
{
	int			msgid;			/**< ��Ϣid. */
	uint32_t	ip_addr;		/**< nat���IP��ַ. */
	uint16_t	port;			/**< nat���IP�˿�. */
};


/// ��������Ԫ�ظ���.
#define ARRAY_SIZE(x)	(sizeof(x)/sizeof((x)[0]))

/**
 * ����������ȡIP
 *
 * @param domain	����������.
 * @param pip_addr	ip��ַ������.
 * @param ip_num    ip��ַ�������Ĵ�С.
 *
 */
int np_get_server_ip(const char* pdomain, uint32_t pip_addr[], int ip_num);

/**
 * ��ȡ�ַ�����ʽ����������
 * @param network_type	��������
 * 
 */
const char *get_string_network_type(int network_type);

#ifdef __cplusplus
};
#endif
#endif