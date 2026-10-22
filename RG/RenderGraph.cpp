#include "RenderGraph.h"
#include "Accessor.h"
#include "AccessorEnums.h"
#include "ResourceConcept.h"
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <iostream>

namespace Ccino {
    bool IsWritable(ResourceAccessFlags accessFlags) {
        bool isWritable = false;
        std::visit([&](const auto& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, BufferAccessFlags>) {
                isWritable = static_cast<bool>(arg & BufferAccessFlagBits::Write);
            } else if constexpr (std::is_same_v<T, TextureAccessFlags>) {
                // TODO Texture
            }
        }, accessFlags);
        return isWritable;
    }

    bool IsReadable(ResourceAccessFlags accessFlags) {
        bool isReadable = false;
        std::visit([&](const auto& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, BufferAccessFlags>) {
                isReadable = static_cast<bool>(arg & BufferAccessFlagBits::Read);
            } else if constexpr (std::is_same_v<T, TextureAccessFlags>) {
                // TODO Texture
            }
        }, accessFlags);
        return isReadable;
    }

    void RGBuilder::Init(RHI::Context* context) {
        m_context = context;
    }

    void RGBuilder::BuildGraph() {

        //TODO: (P2) 将这里的堆分配容器放到栈上
        std::unordered_map<RID, std::vector<RID>>  dependecyGraph;
        std::vector<RGPassDependency> passDependencies;
        std::unordered_map<RID, uint32_t> passInDegrees;

        std::unordered_map<RID, std::unordered_map<uint32_t,std::pair<RID, ResourceAccessFlags>>> producers; //[resourceRID][version] -> passRID, ResourceAccessFlags
        std::unordered_map<RID, std::unordered_map<uint32_t,std::vector<std::pair<RID, ResourceAccessFlags>>>> readers; // [resourceRID][version] -> {passRID, ResourceAccessFlags}

        for (auto& edge : m_edges) {
            if (IsWritable(edge.resourceAccessFlags)) {
                producers[edge.resourceRID][edge.version] = {edge.passRID, edge.resourceAccessFlags};
            } else if (IsReadable(edge.resourceAccessFlags)) {
                readers[edge.resourceRID][edge.version].push_back({edge.passRID, edge.resourceAccessFlags});
            }
            //TODO: Texture
        }

        //WAR
        //这里的含义是： 当前的所有的Producer 的资源会对应一个消费者也就是Reader，并且Producer 的PassRID是唯一的也就是writer.first 的PassRID 找到对应的Reader
        for (auto& [resourceRID, versionMap] : producers) {
            for(auto& [version, writer] : versionMap) {
                for (const auto& [readerPassRID, readerAccessFlags] : readers[resourceRID][version + 1]) {
                    passDependencies.push_back(RGPassDependency{writer.first, readerPassRID, resourceRID, version + 1, writer.second, readerAccessFlags});
                }
            }
        }        

        //RAW
        for (auto& [resourceRID, versionMap] : readers) {
            for (auto& [version, passUses] : versionMap) {
                for (const auto& [passRID, accessFlags] : passUses) {
                    if (producers[resourceRID].contains(version)) {
                        const auto& writer = producers[resourceRID][version];
                        passDependencies.push_back(RGPassDependency{passRID, writer.first, resourceRID, version, accessFlags, writer.second});
                    }
                }
            }
        }

        //WRW
        for (auto& [resourceRID, versionMap] : producers) {
            for(auto& [version, writer] : versionMap) {
                if (producers[resourceRID].contains(version + 1)) {
                    const auto& nextWriter = producers[resourceRID][version + 1];
                    passDependencies.push_back(RGPassDependency{writer.first, nextWriter.first, resourceRID, version + 1, writer.second, nextWriter.second});
                }
            }
        }        
        
        //Buffer Barrier
        std::unordered_map<RID, std::vector<RGBufferBarrier>> bufferBarrierMap; 

        // 初始化所有的Pass的入度 
        for (const auto& dependency : passDependencies) {
            if (dependency.srcPassRID != RID::Invalid() && dependency.dstPassRID != RID::Invalid()) {
                dependecyGraph[dependency.srcPassRID].push_back(dependency.dstPassRID);
                passInDegrees[dependency.dstPassRID]++; //入度+1
                //Barrier注入 这里先不做，后续注册, 需要向RHI转换
                auto* resource = m_resources.Get(dependency.resourceRID);
                if (resource->resourceType == ResourceType::Buffer) {
                    RGBufferBarrier bufferBarrier;
                    std::visit([&](const auto& resourceFlags){
                        if constexpr (std::is_same_v<BufferAccessFlags, std::decay_t<decltype(resourceFlags)>>) {
                            bufferBarrier.dstFlags = resourceFlags;
                        }
                    }, dependency.dstAccessFlags);
                    
                    std::visit([&](const auto& resourceFlags){
                        if constexpr (std::is_same_v<BufferAccessFlags, std::decay_t<decltype(resourceFlags)>>) {
                            bufferBarrier.srcFlags = resourceFlags;
                        }
                    }, dependency.srcAccessFlags);
                    bufferBarrier.srcPassType = m_passes.Get(dependency.srcPassRID)->passType;
                    bufferBarrier.dstPassType = m_passes.Get(dependency.dstPassRID)->passType;

                    bufferBarrier.resourceRID = dependency.resourceRID;

                    bufferBarrierMap[dependency.dstPassRID].push_back(std::move(bufferBarrier));
                }

            }
        }


        m_timeline.clear();
        std::vector<RID> currentLayer;

        for (const auto& passRID : m_passes.GetAllRIDs()) {
            if (passInDegrees[passRID] == 0) {
                currentLayer.push_back(passRID);
            }
        }

        uint32_t scheduledCount = 0;

        while (!currentLayer.empty()) {
            scheduledCount += static_cast<uint32_t>(currentLayer.size());
            m_timeline.push_back(currentLayer);


            std::vector<RID> nextLayer;
            std::unordered_map<RID, RGBufferBarrier> bufferBarrierLayer;

            for (const auto& passRID : currentLayer) {
                for (const auto& dstPassRID : dependecyGraph[passRID]) {
                    for (auto& bufferBarrier : bufferBarrierMap[dstPassRID]) {
                        if (bufferBarrierLayer.contains(bufferBarrier.resourceRID)) {
                            bufferBarrierLayer[bufferBarrier.resourceRID].srcFlags |= bufferBarrier.srcFlags;
                            bufferBarrierLayer[bufferBarrier.resourceRID].dstFlags |= bufferBarrier.dstFlags;
                            bufferBarrierLayer[bufferBarrier.resourceRID].srcPassType |= bufferBarrier.srcPassType;
                            bufferBarrierLayer[bufferBarrier.resourceRID].dstPassType |= bufferBarrier.dstPassType;
                        } else {
                            bufferBarrierLayer[bufferBarrier.resourceRID] = bufferBarrier;
                        }
                    }
                    passInDegrees[dstPassRID]--;
                    std::vector<RGBufferBarrier> currentBufferBarriers;
                    for (auto& [_, barrier] : bufferBarrierLayer) {
                        currentBufferBarriers.push_back(std::move(barrier));
                    }

                    //Buffer
                    m_bufferBarriers.push_back(std::move(currentBufferBarriers));
                    if (passInDegrees[dstPassRID] == 0) {
                        nextLayer.push_back(dstPassRID);
                    }
                }
            }
            currentLayer = std::move(nextLayer);


        }

        if (scheduledCount < m_passes.Size()) {
            for (const auto& [passRID, inDegree] : passInDegrees) {
                if (inDegree > 0) {
                    std::cerr << "RenderGraph build failed, Build a cycle dependency pass: " << passRID.index << std::endl;
                    return;
                }
            }
        }   
    }



    RID RGBuilder::CreateTransientBuffer(typename Buffer::CreateArgs createArgs) {
        auto rid = m_resources.Add(RGResource{ResourceType::Buffer, createArgs});
        return  rid;
    }

    RID RGBuilder::CreateExternalBuffer(const Ref<Buffer>& buffer) {
        auto rid = m_resources.Add(RGResource{ResourceType::Buffer, buffer->GetCreateArgs(), true});
        return rid;
    }



}