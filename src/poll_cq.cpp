
/**
 * Example of RDMA poll_cq. If you have no RDMA hardware,
 * see https://zhuanlan.zhihu.com/p/653997181 to config Soft-RoCE(RXE).
 *
 * g++ poll_cq.cpp -libverbs -lpthread -o poll_cq
 * ./poll_cq
 *
 * author: lihao <hooleeucas@163.com>
 * 
 * Under Apache License 2.0
 */
#include <stdio.h>
#include <endian.h>
#include <infiniband/verbs.h>
#include <stdlib.h>
#include <errno.h>
#include <vector>
#include <functional>
#include <thread>

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

#define PORT_NUM 1

void out_qp_state(struct ibv_qp *qp)
{
    // enum ibv_qp_state {
    //     IBV_QPS_RESET,
    //     IBV_QPS_INIT,
    //     IBV_QPS_RTR,
    //     IBV_QPS_RTS,
    //     IBV_QPS_SQD,
    //     IBV_QPS_SQE,
    //     IBV_QPS_ERR,
    //     IBV_QPS_UNKNOWN
    // };
    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    int ret = ibv_query_qp(qp, &attr, IBV_QP_STATE, &init_attr);
    CHECK(ret == 0, "ibv_query_qp");
    printf("qp=%p, qp_state=%d\n", qp, attr.qp_state);
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
    attr.port_num = PORT_NUM; // 我的机器上必须写成1
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

bool modify_to_rtr(struct ibv_qp *qp, uint32_t r_qpn, uint32_t r_psn, uint16_t dlid, union ibv_gid gid)
{
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_4096;
    attr.dest_qp_num = r_qpn; // TODO
    attr.rq_psn = r_psn;      // TODO
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = 12;
    attr.ah_attr.is_global = 1;
    
    attr.ah_attr.grh.hop_limit = 1;
    attr.ah_attr.grh.dgid = gid; // TODO
    attr.ah_attr.grh.sgid_index = 1; // TODO

    attr.ah_attr.dlid = dlid; // TODO
    // service level, https://www.cnblogs.com/burningTheStar/p/8563347.html
    attr.ah_attr.sl = 0;      // 
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = 1;
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
    printf("enter...\n");
    int ret = ibv_fork_init();
    CHECK(ret == 0, "ibv_fork_init fail");
    struct ibv_device **devs;
    int num_devices, i;
    devs = ibv_get_device_list(&num_devices);
    printf("devs=%p, num_devices=%d\n", devs, num_devices);
    CHECK(devs && num_devices, "ibv_get_device_list fail");
    struct ibv_context *ctx = ibv_open_device(devs[0]);
    CHECK(ctx, "ibv_open_device fail");
    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    CHECK(pd, "ibv_alloc_pd fail");
    struct ibv_comp_channel *ch = ibv_create_comp_channel(ctx);
    CHECK(ch, "ibv_create_comp_channel fail");
    const int cqe = 128;
    struct ibv_cq *cq = ibv_create_cq(ctx, cqe, nullptr, ch, 0);
    CHECK(cq, "ibv_create_cq fail");

    // create qp
    struct ibv_qp *qp1 = create_qp(pd, cq);
    CHECK(qp1, "ibv_create_qp fail");
    struct ibv_qp *qp2 = create_qp(pd, cq);
    CHECK(qp2, "ibv_create_qp fail");
    out_qp_state(qp1);
    out_qp_state(qp2);

    // init qp
    init_qp(qp1);
    init_qp(qp2);
    out_qp_state(qp1);
    out_qp_state(qp2);

    // RTR
    union ibv_gid gid;
    ret = ibv_query_gid(ctx, PORT_NUM, 1, &gid);
    CHECK(ret == 0, "ibv_query_gid fail");
    printf("subnet_prefix=%llu, interface_id=%llu, 0x%llx\n",
        gid.global.subnet_prefix, gid.global.interface_id, gid.global.interface_id);
    // for(int i = 0; i <16; i++) {
    //     printf("%d=%u\n", i, 0xff & gid.raw[i]);
    // }

    struct ibv_port_attr port_attr;
    ret = ibv_query_port(ctx, PORT_NUM, &port_attr);
    CHECK(ret == 0, "ibv_query_port fail");
    const uint32_t psn = 0;
    modify_to_rtr(qp1, qp2->qp_num, psn, port_attr.lid, gid);
    modify_to_rtr(qp2, qp1->qp_num, psn, port_attr.lid, gid);

    out_qp_state(qp1);
    out_qp_state(qp2);

    // RTS
    modify_to_rts(qp1, psn);
    modify_to_rts(qp2, psn);
    out_qp_state(qp1);
    out_qp_state(qp2);

    const uint32_t size = 1024 *1024;
    char *send_buf = (char *)malloc(size);
    CHECK(send_buf, "malloc send_buf fail");
    char *recv_buf = (char *)malloc(size);
    CHECK(recv_buf, "malloc recv_buf fail");
    memset(send_buf, 0, size);
    memset(recv_buf, 0, size);

    std::string hello = "hello rxe RDMA send!";
    memcpy(send_buf, hello.c_str(), hello.size());

    struct ibv_mr *send_mr = ibv_reg_mr(pd, send_buf, size,
                            IBV_ACCESS_LOCAL_WRITE |
                            IBV_ACCESS_REMOTE_WRITE |
                            IBV_ACCESS_REMOTE_READ);
    CHECK(send_mr, "ibv_reg_mr fail");
    printf("send_mr=%p, lkey=%u\n", send_mr, send_mr->lkey);
    struct ibv_mr *recv_mr = ibv_reg_mr(pd, recv_buf, size,
                            IBV_ACCESS_LOCAL_WRITE |
                            IBV_ACCESS_REMOTE_WRITE |
                            IBV_ACCESS_REMOTE_READ);
    CHECK(recv_mr, "ibv_reg_mr fail");
    printf("recv_mr=%p, lkey=%u\n", recv_mr, recv_mr->lkey);

    std::function<void()> polling = [&]() {
        printf("polling thread starting...\n");
        while(1) {
            struct ibv_wc wc;
            int cnt = ibv_poll_cq(cq, 1, &wc);
            CHECK(cnt >= 0, "ibv_poll_cq fail");
            if(cnt == 0) {
                continue;
            }
            if(wc.status != IBV_WC_SUCCESS) {
                /**
                 * Not all wc attributes are always valid. 
                 * If the completion status is other than IBV_WC_SUCCESS,
                 * only the following attributes are valid:
                    wr_id
                    status
                    qp_num
                    vendor_err
                */
                printf("wc.status != IBV_WC_SUCCESS, wc.status=%u. wr_id=%lu\n",
                    wc.status, wc.wr_id);
                continue;
            }
            if(wc.opcode == IBV_WC_SEND) {
                // handle send success
                printf("send success~\n");
            
            } else if(wc.opcode == IBV_WC_RECV) {
                // handle receive success
                std::string msg(recv_buf, size);
                printf("recv success, msg=%s\n", msg.c_str());
            } else {
                printf("unknown wc.opcode == %d\n", wc.opcode);
            }
        }
    };
    // start poll cq
    std::thread poll_thread(polling);

    // qp2 ibv_post_recv
    struct ibv_sge recv_sge;
    recv_sge.addr = (uint64_t)recv_buf;
    recv_sge.length = size;
    recv_sge.lkey = recv_mr->lkey;
    struct ibv_recv_wr recv_wr;
    memset(&recv_wr, 0, sizeof(recv_wr));
    recv_wr.wr_id = 1234;
    recv_wr.sg_list = &recv_sge;
    recv_wr.num_sge = 1;
    struct ibv_recv_wr *bad_recv_wr = nullptr;
    ret = ibv_post_recv(qp2, &recv_wr, &bad_recv_wr);
    CHECK(ret == 0, "ibv_post_recv fail");
    // printf("post_recv succ\n");

    // qp1 ibv_post_send
    struct ibv_sge send_sge;
    send_sge.addr = (uint64_t)send_buf;
    send_sge.length = size;
    send_sge.lkey = send_mr->lkey;
    struct ibv_send_wr send_wr;
    memset(&send_wr, 0, sizeof(send_wr));
    send_wr.wr_id = 5678;
    send_wr.sg_list = &send_sge;
    send_wr.num_sge = 1;
    send_wr.opcode = IBV_WR_SEND;
    send_wr.send_flags = IBV_SEND_SIGNALED;
    struct ibv_send_wr *bad_send_wr = nullptr;
    ret = ibv_post_send(qp1, &send_wr, &bad_send_wr);
    CHECK(ret == 0, "ibv_post_send fail");
    // printf("post_send succ\n");

    poll_thread.join();
    printf("after polling thread\n");
    ibv_dereg_mr(send_mr);
    ibv_dereg_mr(recv_mr);
    free(send_buf);
    free(recv_buf);

    ibv_destroy_qp(qp1);
    ibv_destroy_cq(cq);
    ibv_destroy_comp_channel(ch);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    ibv_free_device_list(devs);

    return 0;
}
