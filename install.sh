#/bin/bash

# Start the ssh-agent and add the GitHub password key
eval $(ssh-agent -s) || exit 1
ssh-add ~/github_password || exit 1


# Install dependencies for building and running the software
sudo apt update
sudo apt -y install \
    libjemalloc-dev \
    libnuma-dev \
    libgoogle-perftools-dev \
    libdb5.3++-dev \
    libmysqld-dev \
    libaio-dev \
    libpmem-dev \
    autoconf \
    lbzip2 \
    gfortran \
    cmake \
    libgflags-dev libsnappy-dev  zlib1g-dev libbz2-dev liblz4-dev libzstd-dev \
    sqlite3 libsqlite3-dev libleveldb-dev liblmdb-dev \
    cloud-utils libomp-dev ocl-icd-opencl-dev \
    cifs-utils \
    msr-tools \
    swig

# Expand the third partition to use all available space
sudo growpart -v /dev/sda 3 ; sudo  resize2fs /dev/sda3

#sudo apt install ca-certificates curl gnupg lsb-release -y
#sudo mkdir -m 0755 -p /etc/apt/keyrings
#curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg
#echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null
#sudo apt update
#sudo apt install docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin -y

# Update to the latest mainline kernel
sudo ubuntu-mainline-kernel.sh -i 6.18.1

# Install YCSB
git clone https://github.com/jsfreischuetz/YCSB-cpp.git
cd YCSB-cpp
git submodule update --init

# YCSB-RocksDB
cd
git clone https://github.com/facebook/rocksdb.git
tmux new-session -d "cd rocksdb ; sudo make install -j"

# YCSB-wiredtiger
git clone https://github.com/wiredtiger/wiredtiger.git
cd wiredtiger
mkdir build
cd build
cmake ../.
tmux new-session -d "make -j ; sudo make install -j"
cd

# YCSB finish up
cd YCSB-cpp ; mkdir build ; cd build ; cmake -DBIND_ROCKSDB=1 -DBIND_WIREDTIGER=1 -DBIND_LMDB=1 -DBIND_LEVELDB=1 -DWITH_SNAPPY=1 -DWITH_LZ4=1 -DWITH_ZSTD=1 .. ; make
cd

# Install Liblinear
wget https://www.csie.ntu.edu.tw/~cjlin/libsvmtools/multicore-liblinear/liblinear-multicore-2.49.zip
unzip liblinear*
cd liblinear-multicore-2.49
make
wget https://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/binary/kddb.bz2
cd

# Install NAS
wget https://www.nas.nasa.gov/assets/npb/NPB3.4.3.tar.gz
tar -xzvf NPB*
cd NPB3.4.3/NPB3.4-OMP/config
cp make.def.template make.def
cp suite.def.template suite.def
sed -i 's/S/D/g' suite.def
cd ..
make suite
cd

# Install XSBench
git clone https://github.com/ANL-CESAR/XSBench.git
cd XSBench/openmp-threading
make
cd

# Install DuckDB
git clone https://github.com/duckdb/duckdb.git
cd duckdb
tmux new-session -d "BUILD_BENCHMARK=1 BUILD_TPCH=1 BUILD_TPCDS=1 make -j"
cd

# Install GapBS
git clone https://github.com/sbeamer/gapbs.git
cd gapbs
sed -i 's/twitter web road kron urand/twitter kron/g' benchmark/bench.mk
tmux new-session -d "make -j ; make bench-graphs -j"
cd

# Clone tiering repositories
git clone git@github.com:jsfreischuetz/tiering_models.git
git clone git@github.com:SujayYadalam94/tiering_solutions.git
cd tiering_solutions
git checkout johannes_kernel
cd

# Install Memeater
git clone https://github.com/host-architecture/colloid.git
sudo apt install -y build-essential git make gawk flex bison libgmp-dev libmpfr-dev libmpc-dev python3 binutils perl libisl-dev libzstd-dev tar gzip bzip2
mkdir ~/gcc-15
cd ~/gcc-15
git clone https://gcc.gnu.org/git/gcc.git gcc-15-source
cd gcc-15-source
git checkout releases/gcc-15.1.0
./contrib/download_prerequisites
cd ~/gcc-15
mkdir gcc-15-build
cd gcc-15-build
../gcc-15-source/configure --prefix=/opt/gcc-15 --disable-multilib --enable-languages=c,c++
tmux new-session -d "make -j ; sudo make install ; sudo update-alternatives --install /usr/bin/gcc gcc /opt/gcc-15/bin/gcc 100 ; sudo update-alternatives --install /usr/bin/g++ g++ /opt/gcc-15/bin/g++ 100"


echo "Once all of the tmux sessions are done, run 'sudo reboot' to use the new kernel."