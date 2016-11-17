/**
 * @file	nat_probe_client.c
 * @author  <wanghao1@xunlei.com>
 * @date	Nov  9 21:07:06 CST 2016
 *
 * @brief	̽��·������NAT���Ϳͻ���
 *
 */



#include "nat_probe.h"
#include "log.h"
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <netdb.h>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <linux/ethtool.h>
#include <linux/if.h>
#include <linux/sockios.h>
#include <sys/ioctl.h>
#include "cjson/cJSON.h"


/// ���ⷢ�����ԵĴ�������Ϊudp���ɿ������ж����Լ���
#define NP_RETRY_SEND_NUM (2)

/// �շ����ĸ�����������NP_PUBLIC_IP_NUMһ��
#define NP_SEND_AND_RECV_NUM (NP_PUBLIC_IP_NUM)

/// ���NAT���͵ļ��ʱ�䣬24Сʱ.
#define NP_NAT_PROBE_INTERVAL_TIME	(24*60*60*1000)

struct np_client_t
{
	int							sock;								/**< socket. */	
	uint32_t					ip_addr[NP_PUBLIC_IP_NUM];			/**< ��׬Ǯ��ͬ��Ӫ�̵�����IP. */
	struct np_request_msg_t		send_msg[NP_SEND_AND_RECV_NUM];		/**< �����͵�����. */
	struct np_response_msg_t	recv_msg[NP_SEND_AND_RECV_NUM];		/**< ���յ�������. */
};

/// ׬Ǯ�����ڵ���������.
static int s_network_type = NP_UNKNOWN;

static int generate_msgid()
{
	static int msgid = 0;

	if (msgid == 0)
	{
		msgid = (int)getpid();
	}
	msgid++;
	XL_DEBUG(EN_PRINT_DEBUG, "msgid: %d", msgid);
	return msgid;
}

static int is_local_ip(const uint32_t ip_addr)
{
	int sock = -1;
	struct ifreq ifr, *it = NULL, *end = NULL;
	struct ifconf ifc;
	struct  sockaddr_in local_addr;
	int addr_len = sizeof(local_addr);
	char tmp_buf[128] = { '\0' };

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1)
	{
		XL_DEBUG(EN_PRINT_ERROR, "create socket failed, err: %s", strerror(errno));
		goto ERR;
	}

	ifc.ifc_len = sizeof(tmp_buf);
	ifc.ifc_buf = tmp_buf;

	// ��ȡÿһ������������
	if (-1 == ioctl(sock, SIOCGIFCONF, &ifc))
	{
		XL_DEBUG(EN_PRINT_ERROR, "ioctl SIOCGIFCONF failed, err: %s\n", strerror(errno));
		goto ERR;
	}

	it = ifc.ifc_req;
	end = it + (ifc.ifc_len / sizeof(struct ifreq));
	for (; it != end; ++it) 
	{
		strcpy(ifr.ifr_name, it->ifr_name);
		if (-1 == ioctl(sock, SIOCGIFFLAGS, &ifr))
		{
			XL_DEBUG(EN_PRINT_ERROR, "ioctl SIOCGIFFLAGS failed, ifr_name: %s, err: %s\n", ifr.ifr_name, strerror(errno));
			continue;
		}
		if ((ifr.ifr_flags & IFF_LOOPBACK))
		{
			continue;
		}
		//��ȡIP��ַ
		if (-1 == ioctl(sock, SIOCGIFADDR, &ifr))
		{
			XL_DEBUG(EN_PRINT_ERROR, "ioctl SIOCGIFADDR failed, ifr_name: %s, err: %s\n", ifr.ifr_name, strerror(errno));
			continue;
		}
		memcpy(&local_addr, &ifr.ifr_addr, addr_len);
		XL_DEBUG(EN_PRINT_DEBUG, "nic name: %s, ip_addr: %u, ip_addr: %u", ifr.ifr_name, local_addr.sin_addr.s_addr, ip_addr);
		if (local_addr.sin_addr.s_addr == ip_addr)
		{
			close(sock);
			return 1;
		}
	}
	close(sock);
	return 0;
ERR:
	close(sock);
	return 0;
}

static int create_socket()
{
	struct timeval timeout = { 1, 0 };
	int sock = -1, retval = -1;
	//int enable = 1;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
	{
		XL_DEBUG(EN_PRINT_ERROR, "call socket() failed, err: %s", strerror(errno));
		goto ERR;
	}
	/// �����յ��ĳ�ʱʱ��
	retval = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	if (retval < 0)
	{
		XL_DEBUG(EN_PRINT_ERROR, "call setsockopt() failed, set SO_RCVTIMEO failed, err: %s", strerror(errno));
		goto ERR;
	}
	return sock;
ERR:
	if (sock >= 0)
	{
		close(sock);
	}
	return -1;
}

static int send_msg(struct np_client_t *pnp_client, int idx, const struct sockaddr_in *pdst_addr)
{
	assert(pnp_client != NULL);
	assert(pdst_addr != NULL);

	cJSON *proot = NULL;
	struct np_request_msg_t *psend = &(pnp_client->send_msg[idx]);
	char *psend_msg = NULL;
	int send_len = 0, tmp_len = 0;
	socklen_t addr_len = sizeof(struct sockaddr_in);

	proot = cJSON_CreateObject();
	cJSON_AddNumberToObject(proot, "msgid", psend->msgid);
	cJSON_AddNumberToObject(proot, "network_type", psend->network_type);
	psend_msg = cJSON_Print(proot);
	cJSON_Delete(proot);

	send_len = strlen(psend_msg);

	while (1)
	{
		tmp_len = sendto(pnp_client->sock, psend_msg, send_len, 0, (const struct sockaddr *)pdst_addr, addr_len);
		if (-1 == tmp_len)
		{
			if (errno == EINTR)
			{
				continue;
			}
			else
			{
				XL_DEBUG(EN_PRINT_ERROR, "call sendto() failed, tmp_len: %d, err: %s", tmp_len, strerror(errno));
				goto ERR;
			}
		}
		if (send_len != tmp_len)
		{
			XL_DEBUG(EN_PRINT_ERROR, "call sendto() failed, tmp_len: %d, send_len: %d", tmp_len, send_len);
			goto ERR;
		}
		break;
	}

	free(psend_msg);
	return 0;
ERR:
	free(psend_msg);
	return -1;
}

static int recv_msg(struct np_client_t *pnp_client, int idx)
{
	assert(pnp_client != NULL);
	char *precv_msg = NULL;
	int tmp_len = 0;
	cJSON *proot = NULL, *pmsgid = NULL,  *pip = NULL, *pport = NULL;
	struct np_response_msg_t *precv = &(pnp_client->recv_msg[idx]);

	precv_msg = malloc(128);
	if (NULL == precv_msg)
	{
		XL_DEBUG(EN_PRINT_ERROR, "call malloc() failed, err: %s", strerror(errno));
		goto ERR;
	}
	while (1)
	{
		tmp_len = recvfrom(pnp_client->sock, precv_msg, 128, 0, NULL, NULL);
		if (tmp_len == -1)
		{
			if (errno == EINTR)
			{
				continue;
			}
			else
			{
				XL_DEBUG(EN_PRINT_DEBUG, "call recvfrom() failed, tmp_len: %d, err: %s", tmp_len, strerror(errno));
				goto ERR;
			}
		}
		break;
	}

	proot = cJSON_Parse(precv_msg);
	if (NULL == proot)
	{
		XL_DEBUG(EN_PRINT_ERROR, "call cJSON_Parse() failed, precv_msg: %s", precv_msg);
		goto ERR;
	}
	pmsgid = cJSON_GetObjectItem(proot, "msgid");
	if (NULL == pmsgid)
	{
		XL_DEBUG(EN_PRINT_ERROR, "get msgid failed, precv_msg: %s", precv_msg);
		goto ERR;
	}
	pip = cJSON_GetObjectItem(proot, "ip");
	if (NULL == pip)
	{
		XL_DEBUG(EN_PRINT_ERROR, "get ip failed, precv_msg: %s", precv_msg);
		goto ERR;
	}
	pport = cJSON_GetObjectItem(proot, "port");
	if (NULL == pport)
	{
		XL_DEBUG(EN_PRINT_ERROR, "get port failed, precv_msg: %s", precv_msg);
		goto ERR;
	}
	precv->msgid = pmsgid->valueint;
	precv->port = pport->valueint;
	precv->ip_addr = strtoul(pip->valuestring, NULL, 10);

	cJSON_Delete(proot);
	free(precv_msg);
	return 0;
ERR:
	cJSON_Delete(proot);
	free(precv_msg);
	return -1;
}

static int np_send_and_recv_msg(struct np_client_t *pnp_client, int network_type, int send_msg_num)
{
	assert(pnp_client != NULL);
	assert(send_msg_num <= NP_PUBLIC_IP_NUM);

	int i = 0, recv_success_num = 0;
	struct sockaddr_in dst_addr;

	for (i = 0; i < send_msg_num; i++)
	{
		memset(&dst_addr, 0, sizeof(dst_addr));
		dst_addr.sin_family = AF_INET;
		dst_addr.sin_port = htons(NP_SERVER_PORT);
		dst_addr.sin_addr.s_addr = pnp_client->ip_addr[i];

		XL_DEBUG(EN_PRINT_DEBUG, "dst_addr, ip: %s", inet_ntoa(dst_addr.sin_addr));
		
		pnp_client->send_msg[i].msgid = generate_msgid();
		pnp_client->send_msg[i].network_type = network_type;

		int retry_num = NP_RETRY_SEND_NUM;
		while (retry_num > 0)
		{
			XL_DEBUG(EN_PRINT_DEBUG, "retry_num: %d", retry_num);
			retry_num--;

			if (-1 == send_msg(pnp_client, i, &dst_addr))
			{
				XL_DEBUG(EN_PRINT_ERROR, "call send_msg() failed");
				return -1;
			}

			if (0 == recv_msg(pnp_client, i))
			{
				if (pnp_client->recv_msg[i].msgid == pnp_client->send_msg[i].msgid)
				{
					// �յ���ȷ�Ļظ�
					recv_success_num += 1;
					break;
				}
			}
			sleep(1);
		}
	}
	if (recv_success_num < send_msg_num)
	{
		XL_DEBUG(EN_PRINT_DEBUG, "recvfrom msg failed");
		return -1;
	}
	return 0;
}

static int np_is_public_network(struct np_client_t *pnp_client, int *pnetwork_type)
{
	assert(pnp_client != NULL);
	assert(pnetwork_type != NULL);

	struct np_response_msg_t *precv = &(pnp_client->recv_msg[0]);
	struct in_addr in = { 0 };

	if (-1 == np_send_and_recv_msg(pnp_client, NP_PUBLIC_NETWORK, 1))
	{
		XL_DEBUG(EN_PRINT_DEBUG, "call np_send_and_recv_msg() failed");
		return -1;
	}
	
	in.s_addr = precv->ip_addr;
	XL_DEBUG(EN_PRINT_NOTICE, "nat after the conversion, ip: %s, port: %u", inet_ntoa(in), precv->port);

	if (!is_local_ip(precv->ip_addr))
	{
		return -1;
	}
	*pnetwork_type = NP_PUBLIC_NETWORK;
	return 0;
}

static int np_is_symmetric_nat(struct np_client_t *pnp_client, int *pnetwork_type)
{
	assert(pnp_client != NULL);
	assert(pnetwork_type != NULL);

	struct np_response_msg_t *precv1 = &(pnp_client->recv_msg[0]);
	struct np_response_msg_t *precv2 = &(pnp_client->recv_msg[1]);
	struct in_addr in = { 0 };

	if (-1 == np_send_and_recv_msg(pnp_client, NP_SYMMETRIC_NAT, 2))
	{
		XL_DEBUG(EN_PRINT_DEBUG, "call np_send_and_recv_msg() failed");
		return -1;
	}
	
	in.s_addr = precv1->ip_addr;
	XL_DEBUG(EN_PRINT_NOTICE, "nat after the conversion 1, ip: %s, port: %u", inet_ntoa(in), precv1->port);
	in.s_addr = precv2->ip_addr;
	XL_DEBUG(EN_PRINT_NOTICE, "nat after the conversion 2, ip: %s, port: %u", inet_ntoa(in), precv2->port);

	if (precv1->ip_addr != precv2->ip_addr || precv1->port != precv2->port)
	{
		*pnetwork_type = NP_SYMMETRIC_NAT;
		return 0;
	}
	return -1;
}

static int np_is_full_cone_nat(struct np_client_t *pnp_client, int *pnetwork_type)
{
	assert(pnp_client != NULL);
	assert(pnetwork_type);

	if (-1 == np_send_and_recv_msg(pnp_client, NP_FULL_CONE_NAT, 1))
	{
		XL_DEBUG(EN_PRINT_DEBUG, "call np_send_and_recv_msg() failed");
		return -1;
	}
	*pnetwork_type = NP_FULL_CONE_NAT;
	return 0;
}

static int np_is_restricted_cone_nat(struct np_client_t *pnp_client, int *pnetwork_type)
{
	assert(pnp_client != NULL);
	assert(pnetwork_type);

	if (-1 == np_send_and_recv_msg(pnp_client, NP_RESTRICTED_CONE_NAT, 1))
	{
		XL_DEBUG(EN_PRINT_DEBUG, "call np_send_and_recv_msg() failed");
		return -1;
	}
	*pnetwork_type = NP_RESTRICTED_CONE_NAT;
	return 0;
}

static int np_is_port_restricted_cone_nat(struct np_client_t *pnp_client, int *pnetwork_type)
{
	assert(pnp_client != NULL);
	assert(pnetwork_type);

	if (-1 == np_send_and_recv_msg(pnp_client, NP_PORT_RESTRICTED_CONE_NAT, 1))
	{
		XL_DEBUG(EN_PRINT_DEBUG, "call np_send_and_recv_msg() failed");
		return -1;
	}
	*pnetwork_type = NP_PORT_RESTRICTED_CONE_NAT;
	return 0;
}

static void np_nat_type_probe()
{
	int network_type = NP_UNKNOWN;
	struct np_client_t np_client;

	memset(&np_client, 0, sizeof(np_client));

	np_client.sock = create_socket();
	if (np_client.sock == -1)
	{
		goto EXIT;
	}

	if (-1 == np_get_server_ip(NP_DOMAIN, np_client.ip_addr, ARRAY_SIZE(np_client.ip_addr)))
	{
		XL_DEBUG(EN_PRINT_ERROR, "call np_get_server_ip() failed");
		goto EXIT;
	}

	if (0 == np_is_public_network(&np_client, &network_type))
	{
		goto EXIT;
	}
	if (0 == np_is_symmetric_nat(&np_client, &network_type))
	{
		goto EXIT;
	}
	if (0 == np_is_full_cone_nat(&np_client, &network_type))
	{
		goto EXIT;
	}
	if (0 == np_is_restricted_cone_nat(&np_client, &network_type))
	{
		goto EXIT;
	}
	if (0 == np_is_port_restricted_cone_nat(&np_client, &network_type))
	{
		goto EXIT;
	}
EXIT:
	if (network_type != NP_UNKNOWN && s_network_type != network_type)
	{
		s_network_type = network_type;
	}
	if (np_client.sock >= 0)
	{
		close(np_client.sock);
	}
}

int main(int __attribute__((unused))argc, char __attribute__((unused))*argv[])
{
	configure_log(EN_PRINT_NOTICE, NULL, 1);
	np_nat_type_probe();
	XL_DEBUG(EN_PRINT_NOTICE, "nat type is %s", get_string_network_type(s_network_type));
	destroy_log();
	return 0;
}
