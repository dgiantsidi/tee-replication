### How to build?
`mkdir build && cd build && cmake .. && make`
### How to run?
`sudo ./bench_martha --process_id=1 --reqs_num=1000000`
`sudo ./bench_donna --process_id=0 --reqs_num=1000000`
`sudo ./bench_martha --process_id=1 --authenticator_mode=0`

```
 enum authenticators {
         SGX = 0, /* SGX as trusted entity */
         SGX_P = 1, /* SGX w/ persistency */
         SGX_R = 2, /* SGX w/ rollback */
         TNIC = 3 /* T-FPGA (mocked) */
 };
```


If you want to use the scripts then 
`sh ./make.sh`
`sudo -E sh ./run.sh -r 1000 -p 1 -a 0`
