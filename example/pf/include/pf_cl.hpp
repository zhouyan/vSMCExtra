//============================================================================
// vSMC/example/pf/include/pf_cl.hpp
//----------------------------------------------------------------------------
//                         vSMC: Scalable Monte Carlo
//----------------------------------------------------------------------------
// Copyright (c) 2013-2015, Yan Zhou
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
//
//   Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS AS IS
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//============================================================================

#ifndef VSMC_EXAMPLE_PF_CL_HPP
#define VSMC_EXAMPLE_PF_CL_HPP

#include <vsmc/core/sampler.hpp>
#include <vsmc/opencl/backend_cl.hpp>
#include <vsmc/rngc/philox.h>

#ifdef VSMC_PF_CL_MPI
#include <vsmc/mpi/backend_mpi.hpp>
#endif

#if VSMC_HAS_HDF5
#include <vsmc/utility/hdf5io.hpp>
#endif

#define PF_CV_DO(Res) cv_do(sampler, vsmc::Res, argv, "." #Res);

static const std::size_t DataNum = 100;
static const std::size_t ParticleNum = 1000;
static const std::size_t StateSize = 4 * sizeof(cl_float);

#ifdef VSMC_PF_CL_MPI
using cv_base = vsmc::StateMPI<vsmc::StateCL<StateSize, cl_float>>;
#else
using cv_base = vsmc::StateCL<StateSize, cl_float>;
#endif

class cv : public cv_base
{
    public:
    cv(size_type N) : cv_base(N) {}

    const vsmc::CLMemory &obs_x() const { return obs_x_.data(); }
    const vsmc::CLMemory &obs_y() const { return obs_y_.data(); }

    void read_data(const char *file)
    {
        if (!file)
            return;

        vsmc::Vector<cl_float> x(DataNum);
        vsmc::Vector<cl_float> y(DataNum);
        std::ifstream data(file);
        for (std::size_t i = 0; i != DataNum; ++i)
            data >> x[i] >> y[i];
        data.close();

        obs_x_.resize(DataNum);
        obs_y_.resize(DataNum);
        manager().write_buffer(obs_x(), DataNum, x.data());
        manager().write_buffer(obs_y(), DataNum, y.data());
    }

    private:
    vsmc::CLBuffer<cl_float> obs_x_;
    vsmc::CLBuffer<cl_float> obs_y_;
};

class cv_init : public vsmc::InitializeCL<cv>
{
    public:
    void eval_sp(std::string &kernel_name)
    {
        kernel_name = std::string("cv_init");
    }

    void eval_param(vsmc::Particle<cv> &particle, void *file)
    {
        particle.value().read_data(static_cast<const char *>(file));
    }

    void eval_pre(vsmc::Particle<cv> &particle)
    {
        w_.resize(particle.size());
        w_buffer_.resize(particle.size());

        vsmc::cl_set_kernel_args(kernel(), kernel_args_offset(),
            w_buffer_.data(), particle.value().obs_x(),
            particle.value().obs_y());
    }

    void eval_post(vsmc::Particle<cv> &particle)
    {
        particle.value().manager().read_buffer(
            w_buffer_.data(), particle.size(), w_.data());
        particle.weight().set_log(w_.data());
    }

    private:
    vsmc::Vector<cl_float> w_;
    vsmc::CLBuffer<cl_float> w_buffer_;
};

class cv_move : public vsmc::MoveCL<cv>
{
    public:
    void eval_sp(std::size_t, std::string &kernel_name)
    {
        kernel_name = std::string("cv_move");
    }

    void eval_pre(std::size_t, vsmc::Particle<cv> &particle)
    {
        w_.resize(particle.size());
        w_buffer_.resize(particle.size());

        vsmc::cl_set_kernel_args(kernel(), kernel_args_offset(),
            w_buffer_.data(), particle.value().obs_x(),
            particle.value().obs_y());
    }

    void eval_post(std::size_t, vsmc::Particle<cv> &particle)
    {
        particle.value().manager().read_buffer(
            w_buffer_.data(), particle.size(), w_.data());
        particle.weight().add_log(w_.data());
    }

    private:
    vsmc::Vector<cl_float> w_;
    vsmc::CLBuffer<cl_float> w_buffer_;
};

class cv_est : public vsmc::MonitorEvalCL<cv>
{
    public:
    void eval_sp(std::size_t, std::string &kernel_name)
    {
        kernel_name = std::string("cv_est");
    }
};

inline void cv_do(vsmc::Sampler<cv> &sampler, vsmc::ResampleScheme res,
    char **argv, const std::string &name)
{
    sampler.resample_scheme(res);
    sampler.resample_threshold(0.5);
    sampler.initialize(argv[1]);
    sampler.iterate(DataNum - 1);

    std::stringstream ss;
#ifdef VSMC_PF_CL_MPI
    ss << name << ".r" << sampler.particle().value().world().rank();
#else
    ss << name;
#endif
    std::string rname(ss.str());
    std::string est_file_name(argv[2] + rname + ".txt");
    std::ofstream est_file;
    est_file.open(est_file_name.c_str());
    est_file << sampler << std::endl;
    est_file.close();

#if VSMC_HAS_HDF5
    vsmc::hdf5store(sampler, argv[2] + rname + ".h5", "Sampler");
#endif
}

#endif // VSMC_EXAMPLE_PF_CL_HPP
