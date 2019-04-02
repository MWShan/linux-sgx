/*
 * Copyright (C) 2011-2019 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include "AESMLogicWrapper.h"
#include <iostream>
#include <unistd.h>

#include <cppmicroservices/Bundle.h>
#include <cppmicroservices/BundleContext.h>
#include <cppmicroservices/BundleImport.h>
#include <cppmicroservices/Framework.h>
#include <cppmicroservices/FrameworkFactory.h>
#include <cppmicroservices/FrameworkEvent.h>


#include "sgx_quote.h"
#include "launch_service.h"
#include "pseop_service.h"
#include "epid_quote_service.h"
#include "quote_ex_service.h"
#include "psepr_service.h"
#include "pce_service.h"
#include "network_service.h"

#include "arch.h"
#include "sgx_urts.h"
#include "oal/oal.h"
//#include "sgx_quote_3.h"
//#include "sgx_ql_quote.h"

static long g_launch_service_bundle_id = -1;
static cppmicroservices::BundleContext g_fw_ctx;
using namespace cppmicroservices;
static Framework g_fw = FrameworkFactory().NewFramework();


template <class S>
bool get_service_from_id(long id, std::shared_ptr<S> &service) noexcept
{
    if (!g_fw_ctx || -1 == id)
        return false;
    try
    {
        auto bundle = g_fw_ctx.GetBundle(id);
        auto context = bundle.GetBundleContext();
        auto ref = context.GetServiceReference<S>();
        service = context.GetService(ref);
    }
    catch(...)
    {
        return false;
    }
    return true;
}

extern "C" sgx_status_t get_launch_token(const enclave_css_t *signature,
                                         const sgx_attributes_t *attribute,
                                         sgx_launch_token_t *launch_token)
{
    std::shared_ptr<ILaunchService> service;
    if (!get_service_from_id(g_launch_service_bundle_id, service))
    {
        return SGX_ERROR_SERVICE_UNAVAILABLE;
    }
    return service->get_launch_token(signature, attribute, launch_token);
}

AESMLogicWrapper::AESMLogicWrapper()
    :m_pseop_service_bundle_id(-1),
    m_quote_service_bundle_id(-1),
    m_launch_service_bundle_id(-1),
    m_ecdsa_quote_service_bundle_id(-1)
{
}

aesm_error_t AESMLogicWrapper::init_quote_ex(
                uint32_t att_key_id_size, const uint8_t *att_key_id,
                uint32_t certification_key_type,
                uint8_t **target_info, uint32_t *target_info_size,
                bool refresh_att_key,
                bool b_pub_key_id, size_t *pub_key_id_size, uint8_t **pub_key_id)
{
    uint8_t *output_target_info = new uint8_t[sizeof(sgx_target_info_t)]();
    uint32_t output_target_info_size = sizeof(sgx_target_info_t);
    uint8_t *output_pub_key_id = NULL;
    size_t output_pub_key_id_size = 0;
    if (b_pub_key_id)
    {
        output_pub_key_id = new uint8_t[sizeof(sgx_sha256_hash_t)]();
        output_pub_key_id_size = *pub_key_id_size;
    }

    aesm_error_t result = AESM_SERVICE_NOT_AVAILABLE;

    std::shared_ptr<IQuoteExService> service;
    if (!get_service_from_id(m_ecdsa_quote_service_bundle_id, service))
    {
        delete[] output_target_info;
        if (b_pub_key_id)
            delete[] output_pub_key_id;
        return AESM_SERVICE_NOT_AVAILABLE;
    }

    result = service->init_quote_ex(att_key_id, att_key_id_size, certification_key_type,
                        output_target_info, output_target_info_size, refresh_att_key,
                        output_pub_key_id, &output_pub_key_id_size);
    if (result == AESM_SUCCESS)
    {
        *target_info = output_target_info;
        *target_info_size = output_target_info_size;
        *pub_key_id_size = output_pub_key_id_size;
        *pub_key_id = output_pub_key_id;
    }
    else
    {
        delete[] output_target_info;
        delete[] output_pub_key_id;
    }
    return result;
}

aesm_error_t AESMLogicWrapper::get_quote_size_ex(
                uint32_t att_key_id_size, const uint8_t *att_key_id,
                uint32_t certification_key_type,
                uint32_t *quote_size)
{
    std::shared_ptr<IQuoteExService> service;
    if (!get_service_from_id(m_ecdsa_quote_service_bundle_id, service))
    {
        return AESM_SERVICE_NOT_AVAILABLE;
    }

    return service->get_quote_size_ex(att_key_id, att_key_id_size, certification_key_type,
                                      quote_size);
}

aesm_error_t AESMLogicWrapper::get_quote_ex(
                uint32_t report_size, const uint8_t *report,
                uint32_t att_key_id_size, const uint8_t *att_key_id,
                uint32_t qe_report_info_size, uint8_t *qe_report_info,
                uint32_t quote_size, uint8_t **quote)
{
    uint8_t *output_quote = new uint8_t[quote_size]();

    aesm_error_t result = AESM_SERVICE_NOT_AVAILABLE;

    std::shared_ptr<IQuoteExService> service;
    if (!get_service_from_id(m_ecdsa_quote_service_bundle_id, service))
    {
        delete[] output_quote;
        return AESM_SERVICE_NOT_AVAILABLE;
    }

    result = service->get_quote_ex(report, report_size, att_key_id, att_key_id_size, qe_report_info,
                                   qe_report_info_size, output_quote, quote_size);
    if (result == AESM_SUCCESS)
    {
        *quote = output_quote;
    }
    else
    {
        delete[] output_quote;
    }
    return result;
}

aesm_error_t AESMLogicWrapper::initQuote(uint8_t** target_info,
                uint32_t* target_info_length,
                uint8_t** gid,
                uint32_t* gid_length)
{
    uint8_t *output_target_info = new uint8_t[sizeof(sgx_target_info_t)]();
    uint8_t *output_gid = new uint8_t[sizeof(sgx_epid_group_id_t)]();
    uint32_t output_target_info_length = sizeof(sgx_target_info_t);
    uint32_t output_gid_length = sizeof(sgx_epid_group_id_t);
    aesm_error_t result = AESM_SERVICE_NOT_AVAILABLE;

    std::shared_ptr<IEpidQuoteService> service;
    if (!get_service_from_id(m_quote_service_bundle_id, service))
    {
        delete[] output_target_info;
        delete[] output_gid;
        return AESM_SERVICE_NOT_AVAILABLE;
    }

    result = service->init_quote(output_target_info, output_target_info_length, output_gid, output_gid_length);
    if (result == AESM_SUCCESS)
    {
        *target_info = output_target_info;
        *target_info_length = output_target_info_length;

        *gid = output_gid;
        *gid_length = output_gid_length;
    }
    else
    {
        delete[] output_target_info;
        delete[] output_gid;
    }
    return result;
}

aesm_error_t AESMLogicWrapper::getQuote(uint32_t reportLength, const uint8_t* report,
                               uint32_t quoteType,
                               uint32_t spidLength, const uint8_t* spid,
                               uint32_t nonceLength, const uint8_t* nonce,
                               uint32_t sig_rlLength, const uint8_t* sig_rl,
                               uint32_t bufferSize, uint8_t** quote,
                               bool b_qe_report, uint32_t* qe_reportSize, uint8_t** qe_report)
{
    uint8_t *output_quote = new uint8_t[bufferSize]();
    uint8_t *output_qe_report = NULL;
    uint32_t output_qe_reportSize = 0;
    if (b_qe_report)
    {
        output_qe_report = new uint8_t[sizeof(sgx_report_t)]();
        output_qe_reportSize = sizeof(sgx_report_t);
    }
    aesm_error_t result = AESM_SERVICE_NOT_AVAILABLE;
    std::shared_ptr<IEpidQuoteService> service;
    if (!get_service_from_id(m_quote_service_bundle_id, service))
    {
        delete[] output_quote;
        if (output_qe_report)
            delete[] output_qe_report;
        return AESM_SERVICE_NOT_AVAILABLE;
    }

    result = service->get_quote(report, reportLength,
                                             quoteType,
                                             spid, spidLength,
                                             nonce, nonceLength,
                                             sig_rl, sig_rlLength,
                                             output_qe_report, output_qe_reportSize,
                                             output_quote, bufferSize);
    if (result == AESM_SUCCESS)
    {
        *quote = output_quote;

        *qe_report = output_qe_report;
        *qe_reportSize = output_qe_reportSize;
    }
    else
    {
        delete[] output_quote;
        if (output_qe_report)
            delete[] output_qe_report;
    }
    return result;
}

aesm_error_t AESMLogicWrapper::closeSession(uint32_t sessionId)
{
    std::shared_ptr<IPseopService> service;
    if (!get_service_from_id(m_pseop_service_bundle_id, service))
    {
        return AESM_SERVICE_NOT_AVAILABLE;
    }
    return service->close_session(sessionId);
}

aesm_error_t AESMLogicWrapper::createSession(uint32_t *session_id,
                                      uint8_t **se_dh_msg1,
                                      uint32_t se_dh_msg1_size)
{
    uint8_t *output_se_dh_msg1 = new uint8_t[se_dh_msg1_size]();
    aesm_error_t result = AESM_SERVICE_NOT_AVAILABLE;
    std::shared_ptr<IPseopService> service;
    if (!get_service_from_id(m_pseop_service_bundle_id, service))
    {
        delete [] output_se_dh_msg1;
        return AESM_SERVICE_NOT_AVAILABLE;
    }

    result = service->create_session(session_id, output_se_dh_msg1, se_dh_msg1_size);
    if(result == AESM_SUCCESS)
    {
        *se_dh_msg1 = output_se_dh_msg1;
    }
    else
    {
        delete [] output_se_dh_msg1;
    }
    return result;
}

aesm_error_t AESMLogicWrapper::exchangeReport(uint32_t session_id,
                                       const uint8_t* se_dh_msg2,
                                       uint32_t se_dh_msg2_size,
                                       uint8_t** se_dh_msg3,
                                       uint32_t se_dh_msg3_size )
{
    uint8_t *output_se_dh_msg3 = new uint8_t[se_dh_msg3_size]();
    aesm_error_t result = AESM_SERVICE_NOT_AVAILABLE;
    std::shared_ptr<IPseopService> service;
    if (!get_service_from_id(m_pseop_service_bundle_id, service))
    {
        delete [] output_se_dh_msg3;
        return AESM_SERVICE_NOT_AVAILABLE;
    }

    result = service->exchange_report(session_id,
                                  se_dh_msg2,
                                  se_dh_msg2_size,
                                  output_se_dh_msg3,
                                  se_dh_msg3_size);
    if(result == AESM_SUCCESS)
    {
        *se_dh_msg3 = output_se_dh_msg3;
    }
    else
    {
        delete [] output_se_dh_msg3;
    }
    return result;
}

aesm_error_t AESMLogicWrapper::getLaunchToken(const uint8_t  *measurement,
                                      uint32_t measurement_size,
                                      const uint8_t  *mrsigner,
                                      uint32_t mrsigner_size,
                                      const uint8_t  *se_attributes,
                                      uint32_t se_attributes_size,
                                      uint8_t  **launch_token,
                                      uint32_t *launch_token_size)
{
    uint32_t output_launch_token_size = sizeof(token_t);
    uint8_t *output_launch_token = new uint8_t[sizeof(token_t)]();
    aesm_error_t result = AESM_SERVICE_NOT_AVAILABLE;
    std::shared_ptr<ILaunchService> service;
    if (!get_service_from_id(m_launch_service_bundle_id, service))
    {
        delete [] output_launch_token;
        return AESM_SERVICE_NOT_AVAILABLE;
    }

    result = service->get_launch_token(measurement,
                                    measurement_size,
                                    mrsigner,
                                    mrsigner_size,
                                    se_attributes,
                                    se_attributes_size,
                                    output_launch_token,
                                    output_launch_token_size);
    if(result == AESM_SUCCESS)
    {
        *launch_token = output_launch_token;
        *launch_token_size = output_launch_token_size;
    }
    else
    {
        delete [] output_launch_token;
    }
    return result;
}

aesm_error_t AESMLogicWrapper::invokeService(const uint8_t  *pse_message_req,
                                      uint32_t pse_message_req_size,
                                      uint8_t  **pse_message_resp,
                                      uint32_t pse_message_resp_size)
{
    uint8_t* output_pse_message_resp = new uint8_t[pse_message_resp_size]();
    aesm_error_t result = AESM_SERVICE_NOT_AVAILABLE;
    std::shared_ptr<IPseopService> service;
    if (!get_service_from_id(m_pseop_service_bundle_id, service))
    {
        delete[] output_pse_message_resp;
        return AESM_SERVICE_NOT_AVAILABLE;
    }

    result = service->invoke_service(pse_message_req,
                                 pse_message_req_size,
                                 output_pse_message_resp,
                                 pse_message_resp_size);
    if (result == AESM_SUCCESS)
    {
        *pse_message_resp = output_pse_message_resp;
    }
    else
    {
        delete[] output_pse_message_resp;
    }
    return result;
}

aesm_error_t AESMLogicWrapper::getPsCap(uint64_t* ps_cap)
{
    std::shared_ptr<IPseopService> service;
    if (!get_service_from_id(m_pseop_service_bundle_id, service))
    {
        return AESM_SERVICE_NOT_AVAILABLE;
    }

    return service->get_ps_cap(ps_cap);
}

aesm_error_t AESMLogicWrapper::reportAttestationStatus(uint8_t* platform_info, uint32_t platform_info_size,
        uint32_t attestation_error_code,
        uint8_t** update_info, uint32_t update_info_size)

{
    uint8_t* output_update_info = new uint8_t[update_info_size]();
    aesm_error_t result = AESM_SERVICE_NOT_AVAILABLE;
    std::shared_ptr<IPseopService> service;
    if (!get_service_from_id(m_pseop_service_bundle_id, service))
    {
        delete[] output_update_info;
        return AESM_SERVICE_NOT_AVAILABLE;
    }

     result = service->report_attestation_status(platform_info,platform_info_size,
             attestation_error_code,
             output_update_info, update_info_size);

    //update_info is valid when result is AESM_UPDATE_AVAILABLE
    if (result == AESM_SUCCESS || result == AESM_UPDATE_AVAILABLE)
    {
        *update_info = output_update_info;
    }
    else
    {
        delete[] output_update_info;
    }
    return result;
}

aesm_error_t AESMLogicWrapper::getWhiteListSize(uint32_t* white_list_size)
{
    std::shared_ptr<ILaunchService> service;
    if (!get_service_from_id(m_launch_service_bundle_id, service))
    {
        return AESM_SERVICE_NOT_AVAILABLE;
    }

    return service->get_white_list_size(white_list_size);
}

aesm_error_t AESMLogicWrapper::getWhiteList(uint8_t **white_list,
                                      uint32_t white_list_size)
{
    uint8_t* output_white_list = new uint8_t[white_list_size]();
    aesm_error_t result = AESM_SERVICE_NOT_AVAILABLE;
    std::shared_ptr<ILaunchService> service;
    if (!get_service_from_id(m_launch_service_bundle_id, service))
    {
        delete[] output_white_list;
        return AESM_SERVICE_NOT_AVAILABLE;
    }

    result = service->get_white_list(output_white_list, white_list_size);
    if (result == AESM_SUCCESS)
    {
        *white_list = output_white_list;
    }
    else
    {
        delete[] output_white_list;
    }
    return result;
}

aesm_error_t AESMLogicWrapper::sgxGetExtendedEpidGroupId(uint32_t* x_group_id)
{
    std::shared_ptr<IEpidQuoteService> service;
    if (!get_service_from_id(m_quote_service_bundle_id, service))
    {
        return AESM_SERVICE_NOT_AVAILABLE;
    }

    return service->get_extended_epid_group_id(x_group_id);
}

aesm_error_t AESMLogicWrapper::sgxSwitchExtendedEpidGroup(uint32_t x_group_id)
{
    std::shared_ptr<IEpidQuoteService> service;
    if (!get_service_from_id(m_quote_service_bundle_id, service))
    {
        return AESM_SERVICE_NOT_AVAILABLE;
    }

    return service->switch_extended_epid_group(x_group_id);
}

aesm_error_t AESMLogicWrapper::sgxRegister(uint8_t* buf, uint32_t buf_size, uint32_t data_type)
{
    if(data_type == SGX_REGISTER_WHITE_LIST_CERT){
        std::shared_ptr<ILaunchService> service;
        if (!get_service_from_id(m_launch_service_bundle_id, service))
        {
            return AESM_SERVICE_NOT_AVAILABLE;
        }

        return service->white_list_register(buf, buf_size);
    }else{
        return AESM_PARAMETER_ERROR;
    }
}

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>
#include <dirent.h>
#include "cppmicroservices/GlobalConfig.h"

#ifdef US_PLATFORM_POSIX
#define PATH_SEPARATOR "/"
#else
#define PATH_SEPARATOR "\\"
#endif

#define BUNDLE_SUBFOLDER "bundles"
#include "ssl_compat_wrapper.h"

std::vector<std::string> get_bundles(const std::string &dir_name)
{
    std::vector<std::string> files;
    std::shared_ptr<DIR> directory_ptr(opendir(dir_name.c_str()), [](DIR *dir) { dir &&closedir(dir); });
    struct dirent *dirent_ptr;
    if (!directory_ptr)
    {
        std::cout << "Error opening : " << std::strerror(errno) << dir_name << std::endl;
        return files;
    }

    while ((dirent_ptr = readdir(directory_ptr.get())) != nullptr)
    {
        std::string file_name = std::string(dirent_ptr->d_name);
        size_t length = file_name.length();
        if (file_name.length() <= strlen(US_LIB_EXT))
            continue;
        else if (file_name.substr(length - strlen(US_LIB_EXT), length) != US_LIB_EXT)
            continue;
        files.push_back(dir_name + PATH_SEPARATOR + file_name);
    }
    return files;
}

ae_error_t AESMLogicWrapper::service_start()
{
    //AESMLogic::service_start();

    try
    {
        // Initialize the framework, such that we can call
        // GetBundleContext() later.
        g_fw.Init();
    }
    catch (const std::exception &e)
    {
        std::cout << e.what() << std::endl;
        return AE_FAILURE;
    }

    // The framework inherits from the Bundle class; it is
    // itself a bundle.
    g_fw_ctx = g_fw.GetBundleContext();
    if (!g_fw_ctx)
    {
        std::cerr << "Invalid framework context" << std::endl;
        return AE_FAILURE;
    }
    else
    {
        std::cout << "The path of system bundle: " << g_fw.GetLocation() << std::endl;
    }

    char buf[PATH_MAX] = {0};
    Dl_info dl_info;
    if (0 == dladdr(__builtin_return_address(0), &dl_info) ||
        NULL == dl_info.dli_fname)
        return AE_FAILURE;
    if (strnlen(dl_info.dli_fname, sizeof(buf)) >= sizeof(buf))
        return AE_FAILURE;
    (void)strncpy(buf, dl_info.dli_fname, sizeof(buf));
    std::string aesm_path(buf);
    std::string bundle_dir("");

    size_t i = aesm_path.rfind(PATH_SEPARATOR, aesm_path.length());
    if (i != std::string::npos)
    {
        bundle_dir = aesm_path.substr(0, i) + PATH_SEPARATOR + BUNDLE_SUBFOLDER;
    }
    // Install all bundles contained in the shared libraries
    // given as command line arguments.
#if defined(US_BUILD_SHARED_LIBS)
    try
    {
        for (auto name : get_bundles(bundle_dir))
        {
            g_fw.GetBundleContext().InstallBundles(name);
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
    }
#endif

    try
    {
        // Start the framework itself.
        g_fw.Start();
        auto bundles = g_fw_ctx.GetBundles();
        for (auto &bundle : bundles)
        {
            bundle.Start();
            std::cerr << bundle.GetSymbolicName() << ":" << bundle.GetVersion() << std::endl;
        }

        for (auto &bundle : bundles)
        {
            std::cout << bundle.GetSymbolicName() << std::endl;
            if (bundle.GetSymbolicName() == "le_launch_service_bundle_name"
                && bundle.GetVersion().GetMajor() == ILaunchService::VERSION)
            {
                m_launch_service_bundle_id = bundle.GetBundleId();
                g_launch_service_bundle_id = m_launch_service_bundle_id;
            }
            else if (bundle.GetSymbolicName() == "epid_quote_service_bundle_name"
                && bundle.GetVersion().GetMajor() == IEpidQuoteService::VERSION)
            {
                m_quote_service_bundle_id = bundle.GetBundleId();
            }
            else if (bundle.GetSymbolicName() == "local_pseop_service_bundle_name"
                && bundle.GetVersion().GetMajor() == IPseopService::VERSION)
            {
                m_pseop_service_bundle_id = bundle.GetBundleId();
            }
            else if (bundle.GetSymbolicName() == "ecdsa_quote_service_bundle_name"
                && bundle.GetVersion().GetMajor() == IQuoteExService::VERSION)
            {
                m_ecdsa_quote_service_bundle_id = bundle.GetBundleId();
            }
            else if (bundle.GetSymbolicName() == "pce_service_bundle_name"
                && bundle.GetVersion().GetMajor() == IPceService::VERSION)
            {
                m_pce_service_bundle_id = bundle.GetBundleId();
            }
            else if (bundle.GetSymbolicName() == "psepr_service_bundle_name"
                && bundle.GetVersion().GetMajor() == IPseprService::VERSION)
            {
                m_psepr_service_bundle_id = bundle.GetBundleId();
            }
            else if (bundle.GetSymbolicName() == "linux_network_service_bundle_name"
                && bundle.GetVersion().GetMajor() == INetworkService::VERSION)
            {
                m_network_service_bundle_id = bundle.GetBundleId();
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        return AE_FAILURE;
    }

    crypto_initialize();

    AESM_LOG_INIT();

    AESM_DBG_INFO("aesm service is starting");

    if (-1 != m_network_service_bundle_id)
    {
        std::shared_ptr<INetworkService> service;
        if (!get_service_from_id(m_network_service_bundle_id, service))
            return AE_FAILURE;
        if(AE_SUCCESS != service->start())
            m_network_service_bundle_id = -1;
    }
    if (-1 != m_pseop_service_bundle_id)
    {
        std::shared_ptr<IPseopService> service;
        if (!get_service_from_id(m_pseop_service_bundle_id, service))
            return AE_FAILURE;
        if(AE_SUCCESS != service->start())
            m_pseop_service_bundle_id = -1;
    }
    if (-1 != m_quote_service_bundle_id)
    {
        std::shared_ptr<IEpidQuoteService> service;
        if (!get_service_from_id(m_quote_service_bundle_id, service))
            return AE_FAILURE;
        if(AE_SUCCESS != service->start())
            m_quote_service_bundle_id = -1;
    }
    if (-1 != m_ecdsa_quote_service_bundle_id)
    {
        std::shared_ptr<IQuoteExService> service;
        if (!get_service_from_id(m_ecdsa_quote_service_bundle_id, service))
            return AE_FAILURE;
        if(AE_SUCCESS != service->start())
            m_ecdsa_quote_service_bundle_id = -1;
    }
    if (-1 != m_launch_service_bundle_id)
    {
        std::shared_ptr<ILaunchService> service;
        if (!get_service_from_id(m_launch_service_bundle_id, service))
            return AE_FAILURE;
        if(AE_SUCCESS != service->start())
        {
            m_launch_service_bundle_id = -1;
            g_launch_service_bundle_id = -1;
        }
    }
    AESM_DBG_INFO("aesm service started");


    return AE_SUCCESS;
}

void AESMLogicWrapper::service_stop()
{
    AESM_DBG_INFO("AESMLogicWrapper::service_stop");
    std::shared_ptr<IPseopService> pseop_service;
    if (get_service_from_id(m_pseop_service_bundle_id, pseop_service))
        pseop_service->stop();
    std::shared_ptr<IPseprService> psepr_service;
    if (get_service_from_id(m_psepr_service_bundle_id, psepr_service))
        psepr_service->stop();
    std::shared_ptr<IEpidQuoteService> epid_service;
    if (get_service_from_id(m_quote_service_bundle_id, epid_service))
        epid_service->stop();
    std::shared_ptr<IQuoteExService> ecdsa_service;
    if (get_service_from_id(m_ecdsa_quote_service_bundle_id, ecdsa_service))
        ecdsa_service->stop();
    std::shared_ptr<IPceService> pce_service;
    if (get_service_from_id(m_pce_service_bundle_id, pce_service))
        pce_service->stop();
    std::shared_ptr<ILaunchService> launch_service;
    if (get_service_from_id(m_launch_service_bundle_id, launch_service))
        launch_service->stop();
    std::shared_ptr<INetworkService> network_service;
    if (get_service_from_id(m_network_service_bundle_id, network_service))
        network_service->stop();
    g_fw.Stop();
    g_fw.WaitForStop(std::chrono::minutes(1));
    crypto_cleanup();
    //sleep(60);
    AESM_DBG_INFO("aesm service down");
    AESM_LOG_FINI();
}

#if !defined(US_BUILD_SHARED_LIBS)
CPPMICROSERVICES_IMPORT_BUNDLE(pce_service_bundle_name)
CPPMICROSERVICES_IMPORT_BUNDLE(epid_quote_service_bundle_name)
CPPMICROSERVICES_IMPORT_BUNDLE(ecdsa_quote_service_bundle_name)
CPPMICROSERVICES_IMPORT_BUNDLE(le_launch_service_bundle_name)
CPPMICROSERVICES_IMPORT_BUNDLE(local_pseop_service_bundle_name)
CPPMICROSERVICES_IMPORT_BUNDLE(psepr_service_bundle_name)
CPPMICROSERVICES_IMPORT_BUNDLE(linux_network_service_bundle_name)
#endif