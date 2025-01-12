#include "Shaders.h"

HRESULT VertexShader::Initialize( Microsoft::WRL::ComPtr<ID3D11Device>& device, std::wstring shaderPath, D3D11_INPUT_ELEMENT_DESC* layoutDesc, UINT numElements )
{
    // Compile the vertex shader
    HRESULT hr = CompileShaderFromFile( shaderPath.c_str(), "VS", "vs_5_0", shaderBuffer.GetAddressOf()  );
    if ( FAILED( hr ) )
    {
        ErrorLogger::Log(
            hr,
            "The FX file cannot be compiled.\n"
            "Please run this executable from the directory that contains the FX file."
        );
        return hr;
    }

	// Create the vertex shader
	hr = device->CreateVertexShader(
        shaderBuffer->GetBufferPointer(),
        shaderBuffer->GetBufferSize(),
        nullptr,
        shader.GetAddressOf()
    );
	if ( FAILED( hr ) )
	{
        ErrorLogger::Log( hr, "Failed to create Vertex Shader!" );
		shaderBuffer->Release();
        return hr;
	}

    // Create the input layout
	hr = device->CreateInputLayout(
        layoutDesc,
        numElements,
        shaderBuffer->GetBufferPointer(),
        shaderBuffer->GetBufferSize(),
        inputLayout.GetAddressOf()
    );
	shaderBuffer->Release();
	if ( FAILED( hr ) )
    {
        ErrorLogger::Log( hr, "Failed to create Input Layout." );
        return hr;
    }

	return hr;
}

void Shaders::BindShaders( ID3D11DeviceContext* context, VertexShader& vs, PixelShader& ps ) noexcept
{
    context->VSSetShader( vs.GetShader(), NULL, 0 );
	context->IASetInputLayout( vs.GetInputLayout() );
	context->PSSetShader( ps.GetShader(), NULL, 0 );
}

ID3D11VertexShader* VertexShader::GetShader() const noexcept
{
    return shader.Get();
}

ID3D10Blob* VertexShader::GetBuffer() const noexcept
{
    return shaderBuffer.Get();
}

ID3D11InputLayout* VertexShader::GetInputLayout() const noexcept
{
    return inputLayout.Get();
}

HRESULT PixelShader::Initialize( Microsoft::WRL::ComPtr<ID3D11Device> &device, std::wstring shaderPath )
{
    // Compile the pixel shader
    HRESULT hr = CompileShaderFromFile( shaderPath.c_str(), "PS", "ps_5_0", shaderBuffer.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        ErrorLogger::Log(
            hr,
            "The FX file cannot be compiled.\n"
            "Please run this executable from the directory that contains the FX file."
        );
        return hr;
    }

	// Create the pixel shader
	hr = device->CreatePixelShader(
        shaderBuffer->GetBufferPointer(),
        shaderBuffer->GetBufferSize(),
        nullptr,
        shader.GetAddressOf()
    );
	shaderBuffer->Release();
    if ( FAILED( hr ) )
    {
        ErrorLogger::Log( hr, "Failed to create Pixel Shader!" );
        return hr;
    }
}

ID3D11PixelShader* PixelShader::GetShader() const noexcept
{
    return shader.Get();
}

ID3D10Blob* PixelShader::GetBuffer() const noexcept
{
    return shaderBuffer.Get();
}

HRESULT Shaders::CompileShaderFromFile( std::wstring szFileName, LPCSTR szEntryPoint, LPCSTR szShaderModel, ID3DBlob** ppBlobOut )
{
    HRESULT hr = S_OK;

    DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(DEBUG) || defined(_DEBUG)
    // Set the D3DCOMPILE_DEBUG flag to embed debug information in the shaders.
    // Setting this flag improves the shader debugging experience, but still allows 
    // the shaders to be optimized and to run exactly the way they will run in 
    // the release configuration of this program.
    dwShaderFlags |= D3DCOMPILE_DEBUG;
#endif

    ID3DBlob* pErrorBlob;
    hr = D3DCompileFromFile(
        szFileName.c_str(),
        nullptr,
        nullptr,
        szEntryPoint,
        szShaderModel,
        dwShaderFlags,
        0,
        ppBlobOut,
        &pErrorBlob
    );

    if ( FAILED( hr ) )
    {
        if ( pErrorBlob != nullptr )
            OutputDebugStringA( (char*)pErrorBlob->GetBufferPointer() );

        if ( pErrorBlob )
            pErrorBlob->Release();

        ErrorLogger::Log( hr, "Failed to compile shader from file!" );
        return hr;
    }

    if ( pErrorBlob )
        pErrorBlob->Release();

    return S_OK;
}