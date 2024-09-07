// %BANNER_BEGIN%
// ---------------------------------------------------------------------
// %COPYRIGHT_BEGIN%
// Copyright (c) 2022 Magic Leap, Inc. All Rights Reserved.
// Use of this file is governed by the Software License Agreement,
// located here: https://www.magicleap.com/software-license-agreement-ml2
// Terms and conditions applicable to third-party materials accompanying
// this distribution may also be found in the top-level NOTICE file
// appearing herein.
// %COPYRIGHT_END%
// ---------------------------------------------------------------------
// %BANNER_END%

#pragma once

#include <app_framework/registry.h>
#include <app_framework/render/material.h>
#include <app_framework/shader/magicleap_mesh_vs_program.h>
#include <app_framework/shader/solid_color_fs_program.h>

class MeshVisualizationMaterial final : public ml::app_framework::Material {
public:
  MeshVisualizationMaterial() : Material() {
    SetVertexProgram(
        ml::app_framework::Registry::GetInstance()
            ->GetResourcePool()
            ->LoadShaderFromCode<ml::app_framework::VertexProgram>(ml::app_framework::kMagicLeapMeshVertexShader));
    SetFragmentProgram(
        ml::app_framework::Registry::GetInstance()
            ->GetResourcePool()
            ->LoadShaderFromCode<ml::app_framework::FragmentProgram>(ml::app_framework::kSolidColorFragmentShader));
    SetOverrideVertexColor(false);
  }
  ~MeshVisualizationMaterial() = default;

private:
  MATERIAL_VARIABLE_DECLARE(bool, OverrideVertexColor);
};