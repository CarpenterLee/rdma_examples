/**
 * Example of create and modify qp. See blog https://zhuanlan.zhihu.com/p/655663006
 * for more detail. And If you have no RDMA hardware, see
 * https://zhuanlan.zhihu.com/p/653997181 to config Soft-RoCE(RXE).
 *
 * g++ modify_qp_simple.cpp -libverbs -o modify_qp_simple
 *
 * machine or comand line A:
 * ./modify_qp_simple
 *
 * machine or comand line B:
 * ./modify_qp_simple
 *
 * author: lihao <hooleeucas@163.com>
 * 
 * Under Apache License 2.0
 */

#include <stdio.h>
#include <infiniband/verbs.h>
#include <stdlib.h>
#include <errno.h>
#include <vector>
#include <sstream>
#include <time.h>
#include <iostream>

#define IB_PORT_NUM 1

#define CHECK(c, fmt, ...)                                               \
    do                                                                   \
    {                                                                    \
        if (!(c))                                                        \
        {                                                                \
            printf("%s:%d, %s, errno=%d, %s\n", __FILE__, __LINE__, fmt, \
                   ##__VA_ARGS__, errno, strerror(errno));               \
            exit(-1);                                                    \
        }                                                                \
    } while (0)

// 建联过程需要交换的信息
struct meta
{
    uint32_t qpn;
    uint32_t psn;
    uint32_t lid; // 主要用于ib，RoCE v2中该字段始终为0
    uint8_t gid[16];

    // 为了方便演示，将meta序列化成字符串
    std::string to_str() const
    {
        const int len = 256;
        char buf[len];
        int n = sprintf(buf, "qpn=%u, spn=%u, lid=%u", qpn, psn, lid);
        for (int i = 0; i < 16; i += 2)
        {
            if (i == 0)
            {
                n += sprintf(buf + n, ", gid=%02x%02x", gid[i], gid[i + 1]);
            }
            else
            {
                n += sprintf(buf + n, ":%02x%02x", gid[i], gid[i + 1]);
            }
        }
        return buf;
    }

    // 从字符串反序列化出meta信息
    static meta from_str(const std::string &str)
    {
        meta i;
        int offset = 0;
        int read;
        uint32_t m, n;
        sscanf(str.c_str(), "qpn=%u, spn=%u, lid=%u, gid=%02x%02x%n",
               &i.qpn, &i.psn, &i.lid, &m, &n, &read);
        i.gid[0] = m;
        i.gid[1] = n;
        offset += read;
        for (int j = 2; j < 16; j += 2)
        {
            sscanf(str.c_str() + offset, ":%02x%02x%n", &m, &n, &read);
            i.gid[j] = m;
            i.gid[j + 1] = n;
            offset += read;
        }
        return i;
    }
};

const char *stat_to_str(enum ibv_qp_state s)
{
    switch (s)
    {
    case IBV_QPS_RESET:
        return "IBV_QPS_RESET";
    case IBV_QPS_INIT:
        return "IBV_QPS_INIT";
    case IBV_QPS_RTR:
        return "IBV_QPS_RTR";
    case IBV_QPS_RTS:
        return "IBV_QPS_RTS";
    case IBV_QPS_SQD:
        return "IBV_QPS_SQD";
    case IBV_QPS_SQE:
        return "IBV_QPS_SQE";
    case IBV_QPS_ERR:
        return "IBV_QPS_ERR";
    case IBV_QPS_UNKNOWN:
        return "IBV_QPS_UNKNOWN";
    default:
        return "UNKNOWN";
    }
}
enum ibv_qp_state get_qp_state(struct ibv_qp *qp)
{
    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    int ret = ibv_query_qp(qp, &attr, IBV_QP_STATE, &init_attr);
    CHECK(ret == 0, "ibv_query_qp");
    return attr.qp_state;
}
struct ibv_qp *create_qp(struct ibv_pd *pd, struct ibv_cq *cq)
{
    struct ibv_qp_init_attr init_attr;
    memset(&init_attr, 0, sizeof(init_attr));
    const int io_depth = 32;
    const int max_sge = 30;
    init_attr.send_cq = cq;
    init_attr.recv_cq = cq;
    init_attr.cap.max_send_wr = io_depth;
    init_attr.cap.max_recv_wr = io_depth;
    init_attr.cap.max_send_sge = max_sge;
    init_attr.cap.max_recv_sge = max_sge;
    init_attr.qp_type = IBV_QPT_RC;
    struct ibv_qp *qp = ibv_create_qp(pd, &init_attr);
    CHECK(qp, "ibv_create_qp fail");
    return qp;
}
bool init_qp(struct ibv_qp *qp)
{
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num = IB_PORT_NUM;
    attr.qp_access_flags = 0;
    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE |
                           IBV_ACCESS_REMOTE_READ |
                           IBV_ACCESS_REMOTE_WRITE;
    int ret = ibv_modify_qp(qp, &attr,
                            IBV_QP_STATE |
                                IBV_QP_PKEY_INDEX |
                                IBV_QP_PORT |
                                IBV_QP_ACCESS_FLAGS);
    CHECK(ret == 0, "ibv_modify_qp init fail");
    return true;
}

bool modify_to_rtr(struct ibv_qp *qp, uint32_t r_qpn, uint32_t r_psn, uint16_t dlid, union ibv_gid r_gid)
{
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_4096;
    attr.dest_qp_num = r_qpn; // 从对端获取
    attr.rq_psn = r_psn;      // 从对端获取
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = 12;
    attr.ah_attr.is_global = 1;

    attr.ah_attr.grh.hop_limit = 1;
    attr.ah_attr.grh.dgid = r_gid; // 从对端获取
    attr.ah_attr.grh.sgid_index = 1;

    attr.ah_attr.dlid = dlid; // 从对端获取
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = IB_PORT_NUM;
    int ret = ibv_modify_qp(qp, &attr,
                            IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
                                IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                                IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);
    CHECK(ret == 0, "ibv_modify_qp RTR fail");
    return true;
}
bool modify_to_rts(struct ibv_qp *qp, uint32_t my_psn)
{
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn = my_psn; // TODO
    attr.timeout = 10;
    attr.retry_cnt = 5;
    attr.rnr_retry = 4; /* infinite */
    attr.max_rd_atomic = 1;
    int ret = ibv_modify_qp(qp, &attr,
                            IBV_QP_STATE |
                                IBV_QP_TIMEOUT |
                                IBV_QP_RETRY_CNT |
                                IBV_QP_RNR_RETRY |
                                IBV_QP_SQ_PSN |
                                IBV_QP_MAX_QP_RD_ATOMIC);
    CHECK(ret == 0, "ibv_modify_qp RTS fail");
    return true;
}

int main(int argc, char *argv[])
{
    struct ibv_device **devs;
    int num_devices, i;
    devs = ibv_get_device_list(&num_devices);
    printf("ib devices count=%d\n", num_devices);
    CHECK(devs && num_devices, "ibv_get_device_list fail");

    struct ibv_context *ctx = ibv_open_device(devs[0]);
    CHECK(ctx, "ibv_open_device fail");

    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    CHECK(pd, "ibv_alloc_pd fail");

    struct ibv_comp_channel *ch = ibv_create_comp_channel(ctx);
    CHECK(ch, "ibv_create_comp_channel fail");

    struct ibv_cq *cq = ibv_create_cq(ctx, 128, nullptr, ch, 0);
    CHECK(cq, "ibv_create_cq fail");

    union ibv_gid gid;
    int ret = ibv_query_gid(ctx, IB_PORT_NUM, 1, &gid);
    CHECK(ret == 0, "ibv_query_gid fail");

    struct ibv_port_attr port_attr;
    ret = ibv_query_port(ctx, IB_PORT_NUM, &port_attr);
    CHECK(ret == 0, "ibv_query_port fail");

    // create qp
    struct ibv_qp *qp = create_qp(pd, cq);
    CHECK(qp, "ibv_create_qp fail");
    printf("qp state is: %s\n", stat_to_str(get_qp_state(qp)));

    // 生成自己的meta信息，并通过输出到控制台的方式传递给对方
    meta my_mt;
    my_mt.qpn = qp->qp_num;                         // 只有创建qp之后才能拿到qp_num
    my_mt.psn = time(nullptr) & 0xFFFFFFFF;         // 随机生成一个数字做为自己的psn
    my_mt.lid = port_attr.lid;                      // 自己的lid
    memcpy(my_mt.gid, &gid, sizeof(union ibv_gid)); // 自己的gid
    std::cout << "copy the following data to peer:" << my_mt.to_str() << std::endl;
    // 从控制台读取对方的建联meta信息，生产中可以选择合适的方式完成交换，比如通过TCP
    std::cout << "input peer meta str:";
    std::string s;
    std::getline(std::cin, s);
    meta peer_mt = meta::from_str(s);

    // init qp
    init_qp(qp);
    printf("qp state is: %s\n", stat_to_str(get_qp_state(qp)));

    // modify to RTR
    union ibv_gid r_gid;
    memcpy(&r_gid, &peer_mt.gid, sizeof(union ibv_gid));
    modify_to_rtr(qp, peer_mt.qpn, peer_mt.psn, peer_mt.lid, r_gid);
    printf("qp state is: %s\n", stat_to_str(get_qp_state(qp)));

    // TODO: post recv some RR

    // modify to RTS
    modify_to_rts(qp, my_mt.psn);
    printf("qp state is: %s\n", stat_to_str(get_qp_state(qp)));

    // TODO: barrier

    printf("qp establish success~\n");
    // 此刻qp就可以正常收发数据了，TODO: do post send

    // cleaning
    ibv_destroy_qp(qp);
    ibv_destroy_cq(cq);
    ibv_destroy_comp_channel(ch);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    ibv_free_device_list(devs);

    return 0;
}
