/**
 * Example of query RDMA device info. If you have no RDMA hardware,
 * see https://zhuanlan.zhihu.com/p/653997181 to config Soft-RoCE(RXE).
 *
 * gcc query_device.cpp -libverbs -o query_device
 * ./query_device
 *
 * author: lihao <hooleeucas@163.com>
 * 
 * Under Apache License 2.0
 */
#include <stdio.h>
#include <infiniband/verbs.h>
#include <stdlib.h>
#include <errno.h>

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

int main(int argc, char *argv[])
{
    struct ibv_device **devs;
    int num_devs;
    // 查询RDMA设备列表
    devs = ibv_get_device_list(&num_devs);
    if (devs == nullptr || num_devs == 0)
    {
        printf("NO RDMA device found!\n");
        return 0;
    }
    // 依次查询所有RDMA设备的信息
    for (int i = 0; i < num_devs; i++)
    {
        printf("------- RDMA device %d -------\n", i);
        // 获取设备名字
        const char *name = ibv_get_device_name(devs[i]);
        CHECK(name != nullptr, "ibv_get_device_name fail");
        // 打开设备
        struct ibv_context *ctx = ibv_open_device(devs[i]);
        CHECK(ctx, "ibv_open_device fail");
        // 并查询设备详细信息
        struct ibv_device_attr attr;
        int ret = ibv_query_device(ctx, &attr);
        CHECK(ret == 0, "ibv_query_device fail");
        printf("name           =%s\n"
               "fw_ver         =%s\n"
               "vendor_id      =%u\n"
               "hw_ver         =%u\n"
               "max_qp         =%d\n"
               "max_qp_wr      =%d\n"
               "max_sge        =%d\n"
               "max_sge_rd     =%d\n"
               "max_cq         =%d\n"
               "max_cqe        =%d\n"
               "max_mr         =%d\n"
               "max_mr_size    =%lu\n"
               "max_pd         =%d\n"
               "max_qp_rd_atom =%d\n",
               name,
               attr.fw_ver,
               attr.vendor_id,
               attr.hw_ver,
               attr.max_qp,
               attr.max_qp_wr,
               attr.max_sge,
               attr.max_sge_rd,
               attr.max_cq,
               attr.max_cqe,
               attr.max_mr,
               attr.max_mr_size,
               attr.max_pd,
               attr.max_qp_rd_atom);
        ibv_close_device(ctx);
    }
    ibv_free_device_list(devs);
    return 0;
}
