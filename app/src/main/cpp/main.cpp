#include "meshing_material.h"
#include <app_framework/application.h>
#include <app_framework/components/magicleap_mesh_component.h>
#include <app_framework/components/renderable_component.h>
#include <app_framework/convert.h>
#include <app_framework/gui.h>
#include <ml_head_tracking.h>
#include <ml_input.h>
#include <ml_meshing2.h>
#include <ml_perception.h>
#include <fstream>
#include <unordered_map>
#include <thread>
#include <chrono>

#include "stb_image_write.h"


// for using MLCoordinateFrameUID in std::unordered_map
namespace std {
    template <>
    struct hash<MLCoordinateFrameUID> {
        size_t operator()(const MLCoordinateFrameUID &id) const {
            std::size_t h1 = std::hash<uint64_t>{}(id.data[0]);
            std::size_t h2 = std::hash<uint64_t>{}(id.data[1]);
            return h1 ^ (h2 << 1);
        }
    };
}

// for using MLCoordinateFrameUID in std::unordered_map
bool operator==(const MLCoordinateFrameUID &lhs, const MLCoordinateFrameUID &rhs) {
    return (lhs.data[0] == rhs.data[0]) && (lhs.data[1] == rhs.data[1]);
}

class MeshingApp : public ml::app_framework::Application {
public:
    MeshingApp(struct android_app *state)
            : ml::app_framework::Application(state, std::vector<std::string>{"com.magicleap.permission.SPATIAL_MAPPING"}),
              head_tracker_(ML_INVALID_HANDLE),
              meshing_client_(ML_INVALID_HANDLE),
              input_handle_(ML_INVALID_HANDLE),
              current_mesh_info_request_(ML_INVALID_HANDLE),
              current_mesh_request_(ML_INVALID_HANDLE),
              meshing_lod_(MLMeshingLOD_Medium),
              is_scanning_(false),
              status_message_("Ready") {
        request_extents_.center = {{{0.f, 0.f, 0.f}}};
        request_extents_.rotation = {{{0.f, 0.f, 0.f, 1.f}}};
        request_extents_.extents = {{{10.f, 10.f, 10.f}}};
    }

    void OnResume() override {
        if (ArePermissionsGranted()) {
            GetGui().Show(); // Show the GUI
            status_message_ = "GUI shown";
            mesh_material_ = std::make_shared<MeshVisualizationMaterial>();
            mesh_material_->SetPolygonMode(GL_LINE);
            UNWRAP_MLRESULT(MLMeshingInitSettings(&meshing_settings_));
            meshing_settings_.fill_hole_length = 3.f;
            meshing_settings_.disconnected_component_area = 0.5f;
            meshing_settings_.flags = MLMeshingFlags_ComputeConfidence | MLMeshingFlags_RemoveMeshSkirt;
            UNWRAP_MLRESULT(MLMeshingCreateClient(&meshing_client_, &meshing_settings_));
            UNWRAP_MLRESULT(MLHeadTrackingCreate(&head_tracker_));
            UNWRAP_MLRESULT(MLHeadTrackingGetStaticData(head_tracker_, &head_static_data_));
            UNWRAP_MLRESULT(MLInputCreate(&input_handle_));
            UNWRAP_MLRESULT(SetupControllerInput());

            status_message_ = "Meshing client and input handle initialized";
        }
    }

    void OnPause() override {
        CleanupControllerInput();
        if (MLHandleIsValid(current_mesh_info_request_)) {
            MLMeshingFreeResource(meshing_client_, &current_mesh_info_request_);
            current_mesh_info_request_ = ML_INVALID_HANDLE;
        }

        if (MLHandleIsValid(current_mesh_request_)) {
            MLMeshingFreeResource(meshing_client_, &current_mesh_request_);
            current_mesh_request_ = ML_INVALID_HANDLE;
        }

        if (MLHandleIsValid(input_handle_)) {
            UNWRAP_MLRESULT(MLInputDestroy(input_handle_));
            input_handle_ = ML_INVALID_HANDLE;
        }

        if (MLHandleIsValid(meshing_client_)) {
            UNWRAP_MLRESULT(MLMeshingDestroyClient(meshing_client_));
            meshing_client_ = ML_INVALID_HANDLE;
        }

        if (MLHandleIsValid(head_tracker_)) {
            UNWRAP_MLRESULT(MLHeadTrackingDestroy(head_tracker_));
            head_tracker_ = ML_INVALID_HANDLE;
        }

        mesh_block_nodes_.clear();
        block_requests_.clear();
        mesh_material_.reset();

        status_message_ = "Resources cleaned up on pause";
    }
    void OnUpdate(float) override {
        if (is_scanning_) {
            RecenterExtentsOnHead();

            if (!MLHandleIsValid(current_mesh_info_request_)) {
                RequestMeshInfo();
            } else {
                ReceiveMeshInfoRequest();
            }

            if (!MLHandleIsValid(current_mesh_request_)) {
                if (!block_requests_.empty()) {
                    RequestMesh();
                }
            } else {
                ReceiveMeshRequest();
            }
        }

        // Render the GUI
        UpdateGui();

        // Any additional rendering should happen before this point if you want it in the screenshot.

        // Save a screenshot after everything is rendered, including the GUI.
        if (should_take_screenshot) {
            SaveScreenshot();
            should_take_screenshot = false;
        }
    }


private:
    bool should_take_screenshot = false;
    void UpdateGui() {
        auto &gui = GetGui();
        gui.BeginUpdate();

        if (ImGui::Button("Start Scan")) {
            StartScan();
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop Scan")) {
            StopScan();
        }
        if (ImGui::Button("Save Mesh")) {
            SaveMesh(meshing_client_);
        }

        if (ImGui::Button("Save PointCloud")) {
            SavePointCloud(meshing_client_);
        }
//        if (ImGui::Button("Save Screenshot")) {
//            SaveScreenshot();
//        }
        if (ImGui::Button("Save Screenshot")) {
            should_take_screenshot = true;
        }


        // Display the current status message
        ImGui::Text("Status: %s", status_message_.c_str());

        gui.EndUpdate();
    }

    void StartScan() {
        if (!is_scanning_) {
            is_scanning_ = true;
            status_message_ = "Mesh scanning started";
        }
    }

    void StopScan() {
        if (is_scanning_) {
            is_scanning_ = false;
            status_message_ = "Mesh scanning stopped";
        }
    }
    //..........................................
    void SaveScreenshot() {
        const int width = 2048;  // Adjust based on your target resolution
        const int height = 2048; // Adjust based on your target resolution

        std::vector<unsigned char> pixels(width * height * 4); // RGBA

        // Capture the framebuffer after rendering is complete
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        // Flip the image vertically since OpenGL's origin is at the bottom-left
        std::vector<unsigned char> flipped_pixels(width * height * 4);
        for (int y = 0; y < height; y++) {
            std::copy(
                    pixels.begin() + (height - 1 - y) * width * 4,
                    pixels.begin() + (height - y) * width * 4,
                    flipped_pixels.begin() + y * width * 4
            );
        }

        // Save the image using stb_image_write
        int result = stbi_write_png("/data/data/com.magicleap.capi.sample.meshing/files/Screenshot.png", width, height, 4, flipped_pixels.data(), width * 4);
        if (result == 0) {
            status_message_ = "Failed to save screenshot";
        } else {
            status_message_ = "Screenshot saved to /data/data/com.magicleap.capi.sample.meshing/files/Screenshot.png";
        }
    }
    //=============================================

    void SavePointCloud(MLHandle meshing_client) {
        // Open the file for writing in .ply format
        std::ofstream file("/data/data/com.magicleap.capi.sample.meshing/files/PointCloudOutput.ply");
        if (!file.is_open()) {
            status_message_ = "Failed to open file for writing";
            return;
        }
        status_message_ = "File opened successfully for writing";

        // Modify the meshing settings to request a point cloud
        MLMeshingSettings point_cloud_settings = meshing_settings_;
        point_cloud_settings.flags |= MLMeshingFlags_PointCloud;  // Ensure point cloud flag is set
        UNWRAP_MLRESULT(MLMeshingUpdateSettings(meshing_client, &point_cloud_settings));

        // Define the extents for the mesh request
        MLMeshingExtents extents = {{{0.f, 0.f, 0.f}}, {{0.f, 0.f, 0.f, 1.f}}, {{10.f, 10.f, 10.f}}};
        MLHandle request_handle = ML_INVALID_HANDLE;

        // Request mesh information
        MLResult result = MLMeshingRequestMeshInfo(meshing_client, &extents, &request_handle);
        if (result != MLResult_Ok) {
            status_message_ = "Failed to request mesh info for point cloud";
            return;
        }

        // Retrieve the mesh info
        MLMeshingMeshInfo mesh_info = {};
        result = MLMeshingGetMeshInfoResult(meshing_client, request_handle, &mesh_info);
        if (result != MLResult_Ok) {
            status_message_ = "Failed to get mesh info for point cloud";
            return;
        }

        // Write header for PLY file
        file << "ply\nformat ascii 1.0\n";
        file << "element vertex " << mesh_info.data_count << "\n";
        file << "property float x\nproperty float y\nproperty float z\n";
        file << "property float confidence\n";  // Assume confidence data is available
        file << "end_header\n";

        // Process each block in the mesh info
        for (uint32_t i = 0; i < mesh_info.data_count; ++i) {
            MLMeshingBlockRequest block_request = {mesh_info.data[i].id, MLMeshingLOD_Medium};
            MLMeshingMeshRequest mesh_request = {1, &block_request};

            result = MLMeshingRequestMesh(meshing_client, &mesh_request, &request_handle);
            if (result != MLResult_Ok) {
                continue; // Skip to the next block if request failed
            }

            MLMeshingMesh mesh = {};
            result = MLMeshingGetMeshResult(meshing_client, request_handle, &mesh);
            if (result != MLResult_Ok) {
                continue; // Skip to the next block if retrieval failed
            }

            // Write point cloud data to file
            for (uint32_t j = 0; j < mesh.data_count; ++j) {
                MLMeshingBlockMesh &block_mesh = mesh.data[j];
                for (uint32_t v = 0; v < block_mesh.vertex_count; ++v) {
                    file << block_mesh.vertex[v].x << " " << block_mesh.vertex[v].y << " " << block_mesh.vertex[v].z;

                    if (block_mesh.confidence) { // Include confidence if available
                        file << " " << block_mesh.confidence[v];
                    }

                    file << "\n";
                }
            }

            // Free the resources for the current mesh request
            MLMeshingFreeResource(meshing_client, &request_handle);
        }

        // Close the file and update the status message
        file.close();
        status_message_ = "Point cloud saved to /data/data/com.magicleap.capi.sample.meshing/files/PointCloudOutput.ply";

        // Restore the original mesh settings
        UNWRAP_MLRESULT(MLMeshingUpdateSettings(meshing_client, &meshing_settings_));
    }

    //=================================================
    void SaveMesh(MLHandle meshing_client) {
        // Open the file for writing
        std::ofstream file("/data/data/com.magicleap.capi.sample.meshing/files/MeshOutput.obj");
        if (!file.is_open()) {
            status_message_ = "Failed to open file for writing";
            return;
        }
        status_message_ = "File opened successfully for writing";

        // Define the extents for the mesh request
        MLMeshingExtents extents = {{{0.f, 0.f, 0.f}}, {{0.f, 0.f, 0.f, 1.f}}, {{10.f, 10.f, 10.f}}};
        MLHandle request_handle = ML_INVALID_HANDLE;

        // Request mesh information
        MLResult result = MLMeshingRequestMeshInfo(meshing_client, &extents, &request_handle);
        if (result != MLResult_Ok) {
            status_message_ = "Failed to request mesh info";
            return;
        }

        status_message_ = "Mesh info requested successfully";

        // Retrieve the mesh info
        MLMeshingMeshInfo mesh_info = {};
        const int max_retries = 15;
        int retry_count = 0;
        while (retry_count < max_retries) {
            result = MLMeshingGetMeshInfoResult(meshing_client, request_handle, &mesh_info);
            if (result == MLResult_Ok) {
                break;  // Successfully retrieved mesh info
            } else if (result != MLResult_Pending) {
                status_message_ = "Failed to get mesh info result after retries. Error: " + std::to_string(result);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Wait before retrying
            retry_count++;
        }

        if (result != MLResult_Ok) {
            status_message_ = "Failed to retrieve mesh info. Error: " + std::to_string(result);
            return;
        }

        status_message_ = "Mesh info retrieved successfully";

        uint32_t global_vertex_offset = 0;

        // Process each block in the mesh info
        for (uint32_t i = 0; i < mesh_info.data_count; ++i) {
            MLMeshingBlockRequest block_request = {mesh_info.data[i].id, MLMeshingLOD_Medium};
            MLMeshingMeshRequest mesh_request = {1, &block_request};

            result = MLMeshingRequestMesh(meshing_client, &mesh_request, &request_handle);
            if (result != MLResult_Ok) {
                status_message_ = "Failed to request mesh for block " + std::to_string(i);
                continue;
            }

            MLMeshingMesh mesh = {};
            bool success = false;  // Reset success flag for each block
            retry_count = 0;

            while (retry_count < max_retries && !success) {
                result = MLMeshingGetMeshResult(meshing_client, request_handle, &mesh);
                if (result == MLResult_Ok) {
                    file << "# Successfully got mesh result with retry_count=" << retry_count << std::endl;
                    success = true;  // Set success to true
                    break;  // Exit the loop immediately
                } else if (result != MLResult_Pending) {
                    status_message_ = "Failed to get mesh result after retries. Error: " + std::to_string(result);
                    break;  // Exit the loop on failure
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Wait before retrying
                retry_count++;
            }

// Outside the loop, ensure that you do not proceed to process the mesh if success is false.
            if (!success) {
                continue;  // Skip processing if we failed to retrieve the mesh
            }


            if (result != MLResult_Ok) {
                continue;  // Skip to the next block if mesh result retrieval failed
            }

            // Write mesh data to file
            for (uint32_t j = 0; j < mesh.data_count; ++j) {
                MLMeshingBlockMesh &block_mesh = mesh.data[j];
                if (block_mesh.vertex_count > 0 && block_mesh.index_count > 0) {
                    // Write vertices
                    for (uint32_t v = 0; v < block_mesh.vertex_count; ++v) {
                        file << "v " << block_mesh.vertex[v].x << " " << block_mesh.vertex[v].y << " " << block_mesh.vertex[v].z << "\n";
                    }
                    // Write faces (adjusted for global vertex offset)
                    for (uint32_t idx = 0; idx < block_mesh.index_count; idx += 3) {
                        file << "f " << (block_mesh.index[idx] + 1 + global_vertex_offset) << " "
                             << (block_mesh.index[idx + 1] + 1 + global_vertex_offset) << " "
                             << (block_mesh.index[idx + 2] + 1 + global_vertex_offset) << "\n";
                    }

                    // Update global vertex offset
                    global_vertex_offset += block_mesh.vertex_count;
                }
            }

            // Free the resources for the current mesh request
            MLMeshingFreeResource(meshing_client, &request_handle);
        }

        // Close the file and update the status message
        file.close();
        status_message_ = "Mesh saved to /data/data/com.magicleap.capi.sample.meshing/files/MeshOutput.obj";
    }



//==========================================

    void CleanupControllerInput() {
        if (MLHandleIsValid(input_handle_)) {
            UNWRAP_MLRESULT(MLInputSetControllerCallbacksEx(input_handle_, nullptr, nullptr));
        }
    }

    MLResult SetupControllerInput() {
        MLInputControllerCallbacksEx callbacks;
        MLInputControllerCallbacksExInit(&callbacks);

        callbacks.on_button_click = [](uint8_t controller_id, MLInputControllerButton button, void *data) {
            auto thiz = static_cast<MeshingApp *>(data);
            if (button == MLInputControllerButton_Bumper) {
                thiz->meshing_lod_ = thiz->GetNextLod();
            }
        };
        return MLInputSetControllerCallbacksEx(input_handle_, &callbacks, (void *)this);
    }

    const MLMeshingLOD GetNextLod() const {
        switch (meshing_lod_) {
            case MLMeshingLOD_Minimum:
                return MLMeshingLOD_Medium;
            case MLMeshingLOD_Medium:
                return MLMeshingLOD_Maximum;
            case MLMeshingLOD_Maximum:
                [[fallthrough]];
            default:
                return MLMeshingLOD_Minimum;
        }
    }

    void RecenterExtentsOnHead() {
        MLTransform head_transform = {};
        MLSnapshot *snapshot = nullptr;
        UNWRAP_MLRESULT(MLPerceptionGetSnapshot(&snapshot));
        UNWRAP_MLRESULT(MLSnapshotGetTransform(snapshot, &head_static_data_.coord_frame_head, &head_transform));
        UNWRAP_MLRESULT(MLPerceptionReleaseSnapshot(snapshot));
        request_extents_.center = head_transform.position;
    }

    void RequestMeshInfo() {
        ALOGV("MeshInfoRequest REQUESTING!");
        UNWRAP_MLRESULT(MLMeshingRequestMeshInfo(meshing_client_, &request_extents_, &current_mesh_info_request_));
    }

    void ReceiveMeshInfoRequest() {
        MLMeshingMeshInfo mesh_info = {};
        const MLResult result = MLMeshingGetMeshInfoResult(meshing_client_, current_mesh_info_request_, &mesh_info);
        switch (result) {
            case MLResult_Ok:
                UpdateBlockRequests(mesh_info);
                MLMeshingFreeResource(meshing_client_, &current_mesh_info_request_);
                current_mesh_info_request_ = ML_INVALID_HANDLE;
                return;
            case MLResult_Pending:
                ALOGV("MeshInfoRequest PENDING!");
                return;
            default:
                UNWRAP_MLRESULT(result);
                current_mesh_info_request_ = ML_INVALID_HANDLE;
        }
    }

    void ReceiveMeshRequest() {
        MLMeshingMesh mesh = {};
        const MLResult result = MLMeshingGetMeshResult(meshing_client_, current_mesh_request_, &mesh);
        switch (result) {
            case MLResult_Ok:
                UpdateBlocks(mesh);
                MLMeshingFreeResource(meshing_client_, &current_mesh_request_);
                current_mesh_request_ = ML_INVALID_HANDLE;
                return;
            case MLResult_Pending:
                ALOGV("MeshRequest PENDING!");
                return;
            default:
                UNWRAP_MLRESULT(result);
                current_mesh_request_ = ML_INVALID_HANDLE;
        }
    }

    void RequestMesh() {
        ALOGV("MeshRequest REQUESTING!");
        MLMeshingMeshRequest mesh_request = {};
        mesh_request.request_count = static_cast<int>(block_requests_.size());
        mesh_request.data = block_requests_.data();
        UNWRAP_MLRESULT(MLMeshingRequestMesh(meshing_client_, &mesh_request, &current_mesh_request_));
    }

    std::shared_ptr<ml::app_framework::Node> CreateMeshBlockNode() {
        auto node = std::make_shared<ml::app_framework::Node>();
        auto mesh_comp = std::make_shared<ml::app_framework::MagicLeapMeshComponent>();
        auto renderable = std::make_shared<ml::app_framework::RenderableComponent>(mesh_comp->GetMesh(), mesh_material_);
        node->AddComponent(renderable);
        node->AddComponent(mesh_comp);
        GetRoot()->AddChild(node);
        return node;
    }

    void UpdateBlockRequests(const MLMeshingMeshInfo &mesh_info) {
        block_requests_.clear();
        for (size_t i = 0; i < mesh_info.data_count; ++i) {
            const auto &mesh_data = mesh_info.data[i];
            MLMeshingBlockRequest request = {mesh_data.id, meshing_lod_};
            switch (mesh_data.state) {
                case MLMeshingMeshState_New: {
                    block_requests_.push_back(request);
                    auto new_block = CreateMeshBlockNode();
                    const auto insert_result = mesh_block_nodes_.insert(std::make_pair(mesh_data.id, new_block));
                    if (!insert_result.second) {
                        ALOGV("MLMeshingMeshInfo state New: block already exists: %s",
                              ml::app_framework::to_string(mesh_data.id).c_str());
                    }
                    break;
                }
                case MLMeshingMeshState_Updated: {
                    block_requests_.push_back(request);
                    if (!mesh_block_nodes_.count(mesh_data.id)) {
                        ALOGE("MLMeshingMeshInfo state Updated: nonexistant block %s",
                              ml::app_framework::to_string(mesh_data.id).c_str());
                    }
                    break;
                }
                case MLMeshingMeshState_Deleted: {
                    auto to_remove = mesh_block_nodes_.find(mesh_data.id);
                    if (to_remove != mesh_block_nodes_.end()) {
                        GetRoot()->RemoveChild(to_remove->second);
                        mesh_block_nodes_.erase(to_remove);
                    }
                    break;
                }
                case MLMeshingMeshState_Unchanged: {
                    if (!mesh_block_nodes_.count(mesh_data.id)) {
                        ALOGE("MLMeshingMeshInfo state Unchanged: nonexistant block %s",
                              ml::app_framework::to_string(mesh_data.id).c_str());
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }

    void UpdateBlocks(const MLMeshingMesh &mesh) {
        for (size_t i = 0; i < mesh.data_count; ++i) {
            const auto &mesh_data = mesh.data[i];
            if (!mesh_block_nodes_.count(mesh_data.id)) {
                ALOGE("MLMeshingMesh tried to update nonexistant block %s", ml::app_framework::to_string(mesh_data.id).c_str());
                continue;
            }
            auto mesh_component = mesh_block_nodes_[mesh_data.id]->GetComponent<ml::app_framework::MagicLeapMeshComponent>();
            mesh_component->UpdateMeshWithConfidence(reinterpret_cast<glm::vec3 *>(mesh_data.vertex),
                                                     reinterpret_cast<glm::vec3 *>(mesh_data.normal), mesh_data.confidence,
                                                     mesh_data.vertex_count, mesh_data.index, mesh_data.index_count);
        }
    }

    MLHeadTrackingStaticData head_static_data_;
    MLHandle head_tracker_, meshing_client_, input_handle_;
    MLHandle current_mesh_info_request_, current_mesh_request_;

    MLMeshingSettings meshing_settings_;
    MLMeshingExtents request_extents_;
    MLMeshingLOD meshing_lod_;
    std::vector<MLMeshingBlockRequest> block_requests_;
    std::unordered_map<MLCoordinateFrameUID, std::shared_ptr<ml::app_framework::Node>> mesh_block_nodes_;
    std::shared_ptr<MeshVisualizationMaterial> mesh_material_;
    std::shared_ptr<ml::app_framework::MagicLeapMeshComponent> mesh_component_;

    bool is_scanning_;             // Flag to control scanning state
    std::string status_message_;   // String to store the current status message
};

void android_main(struct android_app *state) {
    MeshingApp app(state);
    app.RunApp();
}
