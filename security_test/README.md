# Cache attack test

## Preparation

Copy configuration files:

```sh
cp files/* armageddon/libflush/libflush/eviction/strategies/
```

## Build

```sh
BUILDROOT=$ROOT/buildroot/build-aarch64
export CROSS_COMPILE=$BUILDROOT/host/bin/aarch64-linux-
export DESTDIR=./to_buildroot-aarch64
export O=$PWD/out-aarch64
export TIME_SOURCE=perf
export ARCH=armv8
export DEVICE_CONFIGURATION=rpi4
export IPC_ID=0
export SHMEM_SIZE=0x1000

make
```

Additional configuration:

* IPC_ID - ID of IPC shared memory: `/dev/crossconhypipc<ID>`, default value is
  0.
* TIME_SOURCE - possible values: `register`, `perf`, `monotonic_clock`,
  `thread_counter` with `register` being most accurate but requires access to
  specific registers e.g. `PMCCNTR_EL0` for ARMv8.
* SHMEM_SIZE - size of shared memory with ID `<IPC_ID>`. Default value is
  0x1000. Can be smaller than real size.

## Run

1. In VM 1 run

    ```sh
    $ cache_test time 0
    Opening /dev/crossconhypipc0
    mmaping /dev/crossconhypipc0
    Libflush init
    Calculating median baseline. Don't evict.
    Calculated median time: 498
    Median time diff from baseline:  34
    ```

    Program should display median time to access all passed cache lines.
    Program calculates rolling median which should be fairly stable. If it isn't
    then test might not work

2. In VM 2 run

    ```sh
    cache_test evict 5
    ```

    This should evict 5th cache line (0th being start of IPC shared memory).
    You should not see any timing differences in VM 1.

3. Stop previous test in VM 2 and run

    ```sh
    cache_test evict 0
    ```

    This should evict 0th cache line which is accessed by VM 1.
    You should see timing difference reported by VM 1 if cache coloring is
    disabled.

You should be able to run this test on only 1 VM but cache coloring won't help
in this case.
