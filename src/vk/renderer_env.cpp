#include "env_cube_spv.hpp"
#include "env_irradiance_spv.hpp"
#include "env_prefilter_spv.hpp"
#include "vk/renderer.hpp"
#include "vk/vkapp.hpp"
#include "vk/vkutil.hpp"

void Renderer::makeComputePass(const void* spv, uint32_t len, const std::vector<VkDescriptorSetLayoutBinding>& binds,
                               uint32_t pcSize, EnvPass& out, uint32_t maxSets) {
    vkMakeDslPool(m_dev, binds, maxSets, out.dsl, out.pool);
    out.pl = pcSize > 0 ? vkMakePipelineLayoutPC(m_dev, out.dsl, VK_SHADER_STAGE_COMPUTE_BIT, pcSize)
                        : vkMakePipelineLayout(m_dev, out.dsl);
    VkShaderModule cm = vkMakeModule(m_dev, spv, len);
    VkPipelineShaderStageCreateInfo cs{};
    cs.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cs.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cs.module = cm;
    cs.pName = "main";
    VkComputePipelineCreateInfo cpc{};
    cpc.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpc.stage = cs;
    cpc.layout = out.pl;
    VK(vkCreateComputePipelines(m_dev, VK_NULL_HANDLE, 1, &cpc, nullptr, &out.pipe));
    vkDestroyShaderModule(m_dev, cm, nullptr);
    VkDescriptorSetAllocateInfo sa{};
    sa.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    sa.descriptorPool = out.pool;
    sa.descriptorSetCount = 1;
    sa.pSetLayouts = &out.dsl;
    VK(vkAllocateDescriptorSets(m_dev, &sa, &out.set));
}

void Renderer::writeEnvSet(VkDescriptorSet set) {
    WriteDescs w;
    w.image(set, 9, m_envSpecInfo);
    w.image(set, 10, m_envIrrInfo);
    w.update(m_dev);
}

void Renderer::setEnvironment(VkApp& app, const float* rgb, uint32_t w, uint32_t h) {
    if (m_envReady)
        return;
    m_envEqW = w;
    m_envEqH = h;
    const uint32_t cubeSize = 256;
    const uint32_t mips = 5;

    // repack RGB->RGBA in a device-local image: Metal rejects linear tiling for 16/32-bit float and lacks R32G32B32_SFLOAT
    const VkFormat eqFmt = VK_FORMAT_R32G32B32A32_SFLOAT;
    const size_t eqPixels = (size_t)w * h;
    std::vector<float> eqRgba(eqPixels * 4);
    for (size_t i = 0; i < eqPixels; ++i) {
        eqRgba[i * 4 + 0] = rgb[i * 3 + 0];
        eqRgba[i * 4 + 1] = rgb[i * 3 + 1];
        eqRgba[i * 4 + 2] = rgb[i * 3 + 2];
        eqRgba[i * 4 + 3] = 1.0f;
    }
    const VkDeviceSize eqBytes = eqPixels * 16;
    VkBuffer eqStg = VK_NULL_HANDLE;
    VkDeviceMemory eqStgMem = VK_NULL_HANDLE;
    vkMakeBuffer(m_dev, m_pdev, eqStg, eqStgMem, eqBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, eqRgba.data());

    VkImageCreateInfo eci{};
    eci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    eci.imageType = VK_IMAGE_TYPE_2D;
    eci.format = eqFmt;
    eci.extent = {w, h, 1};
    eci.mipLevels = 1;
    eci.arrayLayers = 1;
    eci.samples = VK_SAMPLE_COUNT_1_BIT;
    eci.tiling = VK_IMAGE_TILING_OPTIMAL;
    eci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    eci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK(vkCreateImage(m_dev, &eci, nullptr, &m_envEq.img));
    VkMemoryRequirements emr;
    vkGetImageMemoryRequirements(m_dev, m_envEq.img, &emr);
    VkMemoryAllocateInfo emaa{};
    emaa.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    emaa.allocationSize = emr.size;
    emaa.memoryTypeIndex = vkFindMemoryType(m_pdev, emr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK(vkAllocateMemory(m_dev, &emaa, nullptr, &m_envEq.mem));
    VK(vkBindImageMemory(m_dev, m_envEq.img, m_envEq.mem, 0));
    VkImageViewCreateInfo evci{};
    evci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    evci.image = m_envEq.img;
    evci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    evci.format = eqFmt;
    evci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK(vkCreateImageView(m_dev, &evci, nullptr, &m_envEq.view));

    vkMakeCubeImage(m_dev, m_pdev, cubeSize, mips, m_envPre.img, m_envPre.mem, m_envPre.view);
    vkMakeCubeImage(m_dev, m_pdev, cubeSize, 1, m_envIrr.img, m_envIrr.mem, m_envIrr.view);
    m_envPreMips.resize(mips);
    for (uint32_t m = 0; m < mips; ++m)
        vkMakeCubeMipArrayView(m_dev, m_envPre.img, m, m_envPreMips[m]);
    vkMakeCubeMipArrayView(m_dev, m_envIrr.img, 0, m_envIrrArr);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_pdev, &props);
    const float maxAniso = props.limits.maxSamplerAnisotropy;
    vkMakeSamplerEx(m_dev, m_pdev, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false, 0.0f, 1.0f,
                    m_eqSampler);
    vkMakeSamplerEx(m_dev, m_pdev, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, true,
                    (float)(mips - 1), maxAniso, m_cubeSampler);

    m_envSpecInfo = {m_cubeSampler, m_envPre.view, VK_IMAGE_LAYOUT_GENERAL};
    m_envIrrInfo = {m_cubeSampler, m_envIrr.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo eqInfo{m_eqSampler, m_envEq.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo preSam{m_cubeSampler, m_envPre.view, VK_IMAGE_LAYOUT_GENERAL};
    // store through CUBE views (the same views the glass samples): on this driver the 2D_ARRAY view's layer mapping differs from the CUBE view's, mixing scrambles the faces
    VkImageView prefMipView[5] = {};
    for (uint32_t m = 0; m < mips; ++m)
        vkMakeCubeMipView(m_dev, m_envPre.img, m, prefMipView[m]);
    const VkDescriptorImageInfo preImg{VK_NULL_HANDLE, prefMipView[0], VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo irrImg{VK_NULL_HANDLE, m_envIrr.view, VK_IMAGE_LAYOUT_GENERAL};

    const std::vector<VkDescriptorSetLayoutBinding> cubeBinds = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT},
    };
    makeComputePass(env_cube_spv, env_cube_spv_len / 4, cubeBinds, 4, m_pcCube);
    makeComputePass(env_prefilter_spv, env_prefilter_spv_len / 4, cubeBinds, 8, m_pcPref, 5);
    makeComputePass(env_irradiance_spv, env_irradiance_spv_len / 4, cubeBinds, 4, m_pcIrr);

    WriteDescs wd;
    wd.image(m_pcCube.set, 0, eqInfo);
    wd.image(m_pcCube.set, 1, preImg, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    wd.image(m_pcPref.set, 0, preSam);
    wd.image(m_pcPref.set, 1, preImg, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    // one descriptor set per prefilter mip: a set must not be re-updated between dispatches
    VkDescriptorSet prefSet[5];
    prefSet[0] = m_pcPref.set;
    {
        const VkDescriptorSetLayout sl[4] = {m_pcPref.dsl, m_pcPref.dsl, m_pcPref.dsl, m_pcPref.dsl};
        VkDescriptorSetAllocateInfo sa{};
        sa.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        sa.descriptorPool = m_pcPref.pool;
        sa.descriptorSetCount = 4;
        sa.pSetLayouts = sl;
        VK(vkAllocateDescriptorSets(m_dev, &sa, prefSet + 1));
    }
    for (uint32_t m = 1; m < mips; ++m) {
        wd.image(prefSet[m], 0, preSam);
        wd.image(prefSet[m], 1, {VK_NULL_HANDLE, prefMipView[m], VK_IMAGE_LAYOUT_GENERAL}, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    }
    wd.image(m_pcIrr.set, 0, preSam);
    wd.image(m_pcIrr.set, 1, irrImg, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    wd.update(m_dev);

    VkCommandBuffer cmd = app.beginCommands();

    vkImgBarrier(cmd, m_envEq.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
                 VK_ACCESS_TRANSFER_WRITE_BIT, 1, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy bic{};
    bic.bufferOffset = 0;
    bic.bufferRowLength = w;
    bic.bufferImageHeight = h;
    bic.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    bic.imageOffset = {0, 0, 0};
    bic.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, eqStg, m_envEq.img, VK_IMAGE_LAYOUT_GENERAL, 1, &bic);
    vkImgBarrier(cmd, m_envEq.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 1, VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    for (int i = 0; i < 2; ++i)
        vkImgBarrier(cmd, (i == 0) ? m_envPre.img : m_envIrr.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                     (i == 0) ? mips : 1, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 6);

    const uint32_t groups = (cubeSize + 7) / 8;
    const int pcSize = (int)cubeSize;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pcCube.pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pcCube.pl, 0, 1, &m_pcCube.set, 0, nullptr);
    vkCmdPushConstants(cmd, m_pcCube.pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &pcSize);
    vkCmdDispatch(cmd, groups, groups, 6);
    vkImgBarrier(cmd, m_envPre.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                 VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, mips,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 6);

    for (uint32_t mip = 1; mip < mips; ++mip) {
        const uint32_t size = cubeSize >> mip;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pcPref.pipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pcPref.pl, 0, 1, &prefSet[mip], 0, nullptr);
        const int pcPref[2] = {(int)mip, (int)size};
        vkCmdPushConstants(cmd, m_pcPref.pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 8, pcPref);
        vkCmdDispatch(cmd, (size + 7) / 8, (size + 7) / 8, 6);
        vkImgBarrier(cmd, m_envPre.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                     VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, mips,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 6);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pcIrr.pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pcIrr.pl, 0, 1, &m_pcIrr.set, 0, nullptr);
    vkCmdPushConstants(cmd, m_pcIrr.pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &pcSize);
    vkCmdDispatch(cmd, groups, groups, 6);
    vkImgBarrier(cmd, m_envIrr.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                 VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, 1,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 6);
    app.submit(cmd);
    vkFreeBuffer(m_dev, eqStg, eqStgMem);

    m_envReady = true;
    for (auto& inst : m_insts)
        writeEnvSet(inst.set);
}
