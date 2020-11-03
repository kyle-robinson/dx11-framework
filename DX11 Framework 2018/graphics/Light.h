#pragma once
#ifndef LIGHT_H
#define LIGHT_H

#include "RenderableGameObject.h"

class Light : public RenderableGameObject
{
public:
	bool Initialize( ID3D11Device* device, ID3D11DeviceContext* context,
		ConstantBuffer<CB_VS_matrix>& cb_vs_vertexshader );
	void SetConstantBuffer( ConstantBuffer<CB_PS_light>& cb_ps_light );
	void UpdateConstantBuffer( ConstantBuffer<CB_PS_light>& cb_ps_light );
public:
	bool useTexture = true;
	float alphaFactor = 1.0f;
private:
	DirectX::XMFLOAT3 ambientColor = XMFLOAT3( 1.0f, 1.0f, 1.0f );
	float ambientStrength = 0.1f;
	DirectX::XMFLOAT3 lightColor = { 1.0f, 1.0f, 1.0f };
	float lightStrength = 1.0f;
	DirectX::XMFLOAT3 specularColor = { 1.0f, 1.0f, 1.0f };
	float specularIntensity = 1.0f;
	float specularPower = 10.0f;
	float constant = 1.0f;
	float linear = 0.045f;
	float quadratic = 0.0075f;
};

#endif