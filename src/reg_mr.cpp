/**
 * Example of register RDMA memory region. If you have no RDMA hardware,
 * see https://zhuanlan.zhihu.com/p/653997181 to config Soft-RoCE(RXE).
 *
 * g++ reg_mr.cpp -libverbs -o reg_mr
 * ./reg_mr
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
    printf("enter...\n");
    struct ibv_device **devs;
    int num_devices, i;
    devs = ibv_get_device_list(&num_devices);
    CHECK(devs != nullptr, "ibv_get_device_list fail");
    struct ibv_context *ctx = ibv_open_device(devs[0]);
    CHECK(ctx, "ibv_open_device fail");
    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    CHECK(pd, "ibv_alloc_pd fail");

    std::vector<struct ibv_mr *> mrs;
    const int n = 4;
    const int size = 1024 * 1024;
    for (int i = 0; i < n; i++)
    {
        char *buf = (char *)malloc(size);
        CHECK(buf, "malloc fail");
        struct ibv_mr *mr = ibv_reg_mr(pd, buf, size,
                                       IBV_ACCESS_LOCAL_WRITE |
                                           IBV_ACCESS_REMOTE_WRITE |
                                           IBV_ACCESS_REMOTE_READ);
        CHECK(mr, "ibv_reg_mr fail");
        mrs.push_back(mr);
        printf("i=%d, mr=%p, addr=%p, lkey=%u, rkey=%u\n",
               i, mr, mr->addr, mr->lkey, mr->rkey);
    }

    for (auto mr : mrs)
    {
        ibv_dereg_mr(mr);
        free(mr->addr);
    }

    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    ibv_free_device_list(devs);

    return 0;
}
