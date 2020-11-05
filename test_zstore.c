#include "zstore.h"



int main() {
    const char *devs[] = {
        "Nvme0n1",
        "/tmp/mempool"
    };
    int rc = zstore_mkfs(devs , 0);

    return 0;
}