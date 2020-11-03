#include "Graphics.h"
#include "Sampler.h"
#include "Viewport.h"
#include "../resource.h"
#include <map>

std::map<std::string, std::unique_ptr<Bind::Sampler>> samplerStates;

bool Graphics::Initialize( HWND hWnd, int width, int height )
{
	windowWidth = width;
	windowHeight = height;

	if ( !InitializeDirectX( hWnd ) )
		return false;

	if ( !InitializeShaders() )
		return false;

	if ( !InitializeScene() )
		return false;

    for ( int i = 0; i < 3; i++ )
    {
        DirectX::XMFLOAT4X4 worldMatrix;
        DirectX::XMStoreFloat4x4( &worldMatrix, DirectX::XMMatrixIdentity() );
        worldMatricesCube.push_back( worldMatrix );
    }

    imgui.Initialize( hWnd, device.Get(), context.Get() );

	return true;
}

void Graphics::BeginFrame()
{
	// clear render target
    context->OMSetRenderTargets( 1, renderTargetView.GetAddressOf(), depthStencilView.Get() );
	context->ClearRenderTargetView( renderTargetView.Get(), clearColor );
	context->ClearDepthStencilView( depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0 );

	// set render state
	context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	context->OMSetDepthStencilState( depthStencilState.Get(), 0 );
    context->OMSetBlendState( blendState.Get(), NULL, 0xFFFFFFFF );
    rasterizerSolid == true ? context->RSSetState( rasterizerState_Solid.Get() ) :
        context->RSSetState( rasterizerState_Wireframe.Get() );
    samplerAnisotropic == true ? samplerStates["Anisotropic"]->Bind( *this ) : samplerStates["Point"].get()->Bind( *this );

    // setup constant buffers
    if ( !cb_vs_fog.ApplyChanges() ) return;
	context->VSSetConstantBuffers( 1, 1, cb_vs_fog.GetAddressOf() );
	context->PSSetConstantBuffers( 1, 1, cb_vs_fog.GetAddressOf() );

    light.UpdateConstantBuffer( cb_ps_light );
	if ( !cb_ps_light.ApplyChanges() ) return;
	context->PSSetConstantBuffers( 2, 1, cb_ps_light.GetAddressOf() );
}

void Graphics::RenderFrame()
{
    // setup sprite masking
    if ( useMask ) 
    {
        context->OMSetDepthStencilState( depthStencilState_drawMask.Get(), 0 );
        context->VSSetShader( vertexShader_2D.GetShader(), NULL, 0 );
        context->IASetInputLayout( vertexShader_2D.GetInputLayout() );
        context->PSSetShader( pixelShader_2D_discard.GetShader(), NULL, 0 );
        circleMask == true ? circle.Draw( camera2D.GetWorldMatrix() * camera2D.GetOrthoMatrix() ) :
            square.Draw( camera2D.GetWorldMatrix() * camera2D.GetOrthoMatrix() );
        context->OMSetDepthStencilState( depthStencilState_writeMask.Get(), 0 );
    }
	
    // setup shaders
	context->VSSetShader( vertexShader_light.GetShader(), NULL, 0 );
	context->IASetInputLayout( vertexShader_light.GetInputLayout() );
	context->PSSetShader( pixelShader_light.GetShader(), NULL, 0 );

    // render models
    nanosuit.Draw( camera3D.GetViewMatrix(), camera3D.GetProjectionMatrix() );

    UINT offset = 0;
    context->IASetVertexBuffers( 0, 1, vertexBufferCube.GetAddressOf(), vertexBufferCube.StridePtr(), &offset );
    context->IASetIndexBuffer( indexBufferCube.Get(), DXGI_FORMAT_R16_UINT, 0 );
    context->PSSetShaderResources( 0, 1, boxTexture.GetAddressOf() );
    for ( int i = 0; i < worldMatricesCube.size(); i++ )
    {
        DirectX::XMMATRIX worldMatrix = DirectX::XMLoadFloat4x4( &worldMatricesCube[i] );
        cb_vs_matrix.data.worldMatrix = worldMatrix;
        cb_ps_light.data.useTexture = light.useTexture;
        if ( !cb_vs_matrix.ApplyChanges() ) return;
        if ( !cb_ps_light.ApplyChanges() ) return;
        context->VSSetConstantBuffers( 0, 1, cb_vs_matrix.GetAddressOf() );
        context->DrawIndexed( indexBufferCube.IndexCount(), 0, 0 );
    }

	context->PSSetShader( pixelShader_noLight.GetShader(), NULL, 0 );
	light.Draw( camera3D.GetViewMatrix(), camera3D.GetProjectionMatrix() );
}

void Graphics::EndFrame()
{
    // render to texture
    context->OMSetRenderTargets( 1, backBuffer.GetAddressOf(), nullptr );
    context->ClearRenderTargetView( backBuffer.Get(), clearColor );

    UINT offset = 0;
    context->PSSetShaderResources( 0, 1, shaderResourceView.GetAddressOf() );
    context->IASetVertexBuffers( 0, 1, vertexBufferFullscreen.GetAddressOf(), vertexBufferFullscreen.StridePtr(), &offset );
    context->IASetInputLayout( vertexShader_full.GetInputLayout() );
    context->IASetIndexBuffer( indexBufferFullscreen.Get(), DXGI_FORMAT_R16_UINT, 0 );
    context->VSSetShader( vertexShader_full.GetShader(), NULL, 0 );
    context->PSSetShader( pixelShader_full.GetShader(), NULL, 0 );
    
    cb_vs_fullscreen.data.multiView = multiView;
    if ( !cb_vs_fullscreen.ApplyChanges() ) return;
    context->VSSetConstantBuffers( 0, 1, cb_vs_fullscreen.GetAddressOf() );

    if ( rasterizerSolid )
    {
        context->DrawIndexed( indexBufferFullscreen.IndexCount(), 0, 0 );
    }
    else
    {
        context->RSSetState( rasterizerState_Solid.Get() );
        context->DrawIndexed( indexBufferFullscreen.IndexCount(), 0, 0 );
        context->RSSetState( rasterizerState_Wireframe.Get() );
    }

    // display imgui
    imgui.BeginRender();
    imgui.RenderMainWindow( context.Get(), clearColor, light.useTexture, light.alphaFactor,
        rasterizerSolid, samplerAnisotropic, multiView, useMask, circleMask );
    imgui.RenderLightWindow( light, cb_ps_light );
    imgui.RenderFogWindow( cb_vs_fog );
    imgui.EndRender();
    
    // unbind rtv and srv
    context->OMSetRenderTargets( 1, nullRenderTarget.GetAddressOf(), nullptr );
    context->PSSetShaderResources( 0, 1, nullShaderResourceView.GetAddressOf() );
    
    // display frame
	HRESULT hr = swapChain->Present( 1, NULL );
	if ( FAILED( hr ) )
	{
		hr == DXGI_ERROR_DEVICE_REMOVED ?
            ErrorLogger::Log( device->GetDeviceRemovedReason(), "Swap Chain. Graphics device removed!" ) :
            ErrorLogger::Log( hr, "Swap Chain failed to render frame!" );
		exit( -1 );
	}
}

void Graphics::Update( float dt )
{
    // model transformations
    //nanosuit.AdjustRotation( XMFLOAT3( 0.0f, 0.001f * dt, 0.0f ) );

    static float timer = 0.0f;
    static DWORD dwTimeStart = 0;
	DWORD dwTimeCur = GetTickCount64();
	if ( dwTimeStart == 0 ) dwTimeStart = dwTimeCur;
	timer = ( dwTimeCur - dwTimeStart ) / 1000.0f;

    // cube transformations
    DirectX::XMStoreFloat4x4( &worldMatricesCube[0],
        DirectX::XMMatrixScaling( 2.0f, 2.0f, 2.0f ) *
        DirectX::XMMatrixRotationZ( timer * 1.5f ) *
        DirectX::XMMatrixTranslation( 0.0f, 10.0f, 15.0f )
    );
    DirectX::XMStoreFloat4x4( &worldMatricesCube[1],
        DirectX::XMMatrixScaling( 2.0f, 2.0f, 2.0f ) *
        DirectX::XMMatrixTranslation( 10.0f, 10.0f, 0.0f ) *
        DirectX::XMMatrixRotationY( timer )
    );
    DirectX::XMStoreFloat4x4( &worldMatricesCube[2],
        DirectX::XMMatrixScaling( 2.0f, 2.0f, 2.0f ) *
        DirectX::XMMatrixTranslation( 1.0f, 10.0f, 0.0f ) *
        DirectX::XMMatrixRotationZ( timer * 1.5f ) *
        DirectX::XMMatrixTranslation( 0.0f, 7.5f, 0.0f )
    );
}

UINT Graphics::GetWidth() const noexcept
{
    return windowWidth;
}

UINT Graphics::GetHeight() const noexcept
{
    return windowHeight;
}

bool Graphics::InitializeDirectX( HWND hWnd )
{
    try
    {
        UINT createDeviceFlags = 0;
    #ifdef _DEBUG
        createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    #endif

        D3D_DRIVER_TYPE driverTypes[] =
        {
            D3D_DRIVER_TYPE_HARDWARE,
            D3D_DRIVER_TYPE_WARP,
            D3D_DRIVER_TYPE_REFERENCE,
        };
        UINT numDriverTypes = ARRAYSIZE( driverTypes );

        D3D_FEATURE_LEVEL featureLevels[] =
        {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };
	    UINT numFeatureLevels = ARRAYSIZE( featureLevels );

        DXGI_SWAP_CHAIN_DESC sd = { 0 };
        sd.BufferCount = 1;
        sd.BufferDesc.Width = windowWidth;
        sd.BufferDesc.Height = windowHeight;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 1;
        sd.OutputWindow = hWnd;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        HRESULT hr;
        for ( UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++ )
        {
            D3D_DRIVER_TYPE driverType = driverTypes[driverTypeIndex];
            hr = D3D11CreateDeviceAndSwapChain(
                nullptr,                    // IDXGI Adapter
                driverType,                 // Driver Type
                nullptr,                    // Software Module
                createDeviceFlags,          // Flags for Runtime Layers
                featureLevels,              // Feature Levels Array
                numFeatureLevels,           // No. of Feature Levels
                D3D11_SDK_VERSION,          // SDK Version
                &sd,                        // Swap Chain Description
                swapChain.GetAddressOf(),   // Swap Chain Address
                device.GetAddressOf(),      // Device Address
                nullptr,                    // Ptr to Feature Level
                context.GetAddressOf()      // Context Address
            );
            if ( SUCCEEDED( hr ) )
                break;
        }
        COM_ERROR_IF_FAILED( hr, "Failed to create Device and Swap Chain!" );

        // create a render target view with back buffer
        Microsoft::WRL::ComPtr<ID3D11Texture2D> pBackBuffer;
        hr = swapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), (LPVOID*)pBackBuffer.GetAddressOf() );
        COM_ERROR_IF_FAILED( hr, "Failed to create Back Buffer!" );
        hr = device->CreateRenderTargetView( pBackBuffer.Get(), nullptr, backBuffer.GetAddressOf() );
        COM_ERROR_IF_FAILED( hr, "Failed to create Render Target View with Back Buffer!" );

        // create texture resource
        D3D11_TEXTURE2D_DESC textureDesc = { 0 };
        textureDesc.Width = windowWidth;
        textureDesc.Height = windowHeight;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = sd.SampleDesc.Count;
        textureDesc.SampleDesc.Quality = sd.SampleDesc.Quality;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        textureDesc.CPUAccessFlags = 0;
        textureDesc.MiscFlags = 0;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> pTexture;
        hr = device->CreateTexture2D( &textureDesc, nullptr, pTexture.GetAddressOf() );
        COM_ERROR_IF_FAILED( hr, "Failed to create Texture for Render Target!" );

        // create resource view on texture
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
        hr = device->CreateShaderResourceView( pTexture.Get(), &srvDesc, shaderResourceView.GetAddressOf() );
        COM_ERROR_IF_FAILED( hr, "Failed to create Shader Resource View!" );

        // create the target view on the texture
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = textureDesc.Format;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D = D3D11_TEX2D_RTV{ 0 };
        hr = device->CreateRenderTargetView( pTexture.Get(), &rtvDesc, renderTargetView.GetAddressOf() );
        COM_ERROR_IF_FAILED( hr, "Failed to create Render Target View with Texture!" );

        // create depth stencil
        CD3D11_TEXTURE2D_DESC depthStencilDesc(
            DXGI_FORMAT_D24_UNORM_S8_UINT,
            windowWidth,
            windowHeight );
        depthStencilDesc.MipLevels = 1;
        depthStencilDesc.SampleDesc.Count = sd.SampleDesc.Count;
        depthStencilDesc.SampleDesc.Quality = sd.SampleDesc.Quality;
        depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        hr = device->CreateTexture2D( &depthStencilDesc, NULL, depthStencilBuffer.GetAddressOf() );
        COM_ERROR_IF_FAILED( hr, "Failed to create Depth Stencil Buffer!" );
        hr = device->CreateDepthStencilView( depthStencilBuffer.Get(), NULL, depthStencilView.GetAddressOf() );
        COM_ERROR_IF_FAILED( hr, "Failed to create Depth Stencil View!" );

        // set depth stencil states
		CD3D11_DEPTH_STENCIL_DESC depthStencilStateDesc( CD3D11_DEFAULT{} );
		depthStencilStateDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		hr = device->CreateDepthStencilState( &depthStencilStateDesc, depthStencilState.GetAddressOf() );
		COM_ERROR_IF_FAILED( hr, "Failed to create Depth Stencil State!" );

		CD3D11_DEPTH_STENCIL_DESC depthStencilStateDesc_drawMask( CD3D11_DEFAULT{} );
		depthStencilStateDesc_drawMask.DepthEnable = FALSE;
		depthStencilStateDesc_drawMask.StencilEnable = TRUE;
		
		depthStencilStateDesc_drawMask.BackFace.StencilFunc = D3D11_COMPARISON_NEVER;
		depthStencilStateDesc_drawMask.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		depthStencilStateDesc_drawMask.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		depthStencilStateDesc_drawMask.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;

		depthStencilStateDesc_drawMask.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		depthStencilStateDesc_drawMask.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		depthStencilStateDesc_drawMask.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		depthStencilStateDesc_drawMask.FrontFace.StencilPassOp = D3D11_STENCIL_OP_INCR_SAT;

		hr = device->CreateDepthStencilState( &depthStencilStateDesc_drawMask, depthStencilState_drawMask.GetAddressOf() );
		COM_ERROR_IF_FAILED( hr, "Failed to create Depth Stencil State for drawing mask!" );

		CD3D11_DEPTH_STENCIL_DESC depthStencilStateDesc_writeMask( CD3D11_DEFAULT{} );
		depthStencilStateDesc_writeMask.StencilEnable = TRUE;
		depthStencilStateDesc_writeMask.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		
		depthStencilStateDesc_writeMask.BackFace.StencilFunc = D3D11_COMPARISON_NEVER;
		depthStencilStateDesc_writeMask.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		depthStencilStateDesc_writeMask.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		depthStencilStateDesc_writeMask.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;

		depthStencilStateDesc_writeMask.FrontFace.StencilFunc = D3D11_COMPARISON_LESS;
		depthStencilStateDesc_writeMask.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		depthStencilStateDesc_writeMask.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		depthStencilStateDesc_writeMask.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;

		hr = device->CreateDepthStencilState( &depthStencilStateDesc_writeMask, depthStencilState_writeMask.GetAddressOf() );
		COM_ERROR_IF_FAILED( hr, "Failed to create Depth Stencil State for writing mask!" );

        // setup the viewport
        std::unique_ptr<Bind::Viewport> viewport = std::make_unique<Bind::Viewport>( *this );
        viewport.get()->Bind( *this );

        // setup rasterizer states
        CD3D11_RASTERIZER_DESC rasterizerDesc = CD3D11_RASTERIZER_DESC( CD3D11_DEFAULT{} );
        rasterizerDesc.MultisampleEnable = TRUE;
        hr = device->CreateRasterizerState( &rasterizerDesc, rasterizerState_Solid.GetAddressOf() );
        COM_ERROR_IF_FAILED( hr, "Failed to create solid Rasterizer State!" );

        rasterizerDesc.MultisampleEnable = TRUE;
        rasterizerDesc.FillMode = D3D11_FILL_WIREFRAME;
        hr = device->CreateRasterizerState( &rasterizerDesc, rasterizerState_Wireframe.GetAddressOf() );
        COM_ERROR_IF_FAILED( hr, "Failed to create wireframe Rasterizer State!" );

        // set blend state
		D3D11_RENDER_TARGET_BLEND_DESC renderTargetBlendDesc = { 0 };
		renderTargetBlendDesc.BlendEnable = TRUE;
		renderTargetBlendDesc.SrcBlend = D3D11_BLEND_SRC_ALPHA;
		renderTargetBlendDesc.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		renderTargetBlendDesc.BlendOp = D3D11_BLEND_OP_ADD;
		renderTargetBlendDesc.SrcBlendAlpha = D3D11_BLEND_ONE;
		renderTargetBlendDesc.DestBlendAlpha = D3D11_BLEND_ZERO;
		renderTargetBlendDesc.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		renderTargetBlendDesc.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		D3D11_BLEND_DESC blendDesc = { 0 };
		blendDesc.RenderTarget[0] = renderTargetBlendDesc;
		hr = device->CreateBlendState( &blendDesc, blendState.GetAddressOf() );
		COM_ERROR_IF_FAILED( hr, "Failed to create Blend State!" );

        // create sampler states
        samplerStates.emplace( "Anisotropic", std::make_unique<Bind::Sampler>( *this, Bind::Sampler::Type::Anisotropic ) );
        samplerStates.emplace( "Bilinear", std::make_unique<Bind::Sampler>( *this, Bind::Sampler::Type::Bilinear ) );
        samplerStates.emplace( "Point", std::make_unique<Bind::Sampler>( *this, Bind::Sampler::Type::Point ) );
    }
    catch ( COMException& exception )
    {
        ErrorLogger::Log( exception );
        return false;
    }

    return true;
}

bool Graphics::InitializeShaders()
{
    try
    {
        /*   MODELS   */
        D3D11_INPUT_ELEMENT_DESC layoutModel[] = {
		    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		    { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	    };
	    UINT numElements = ARRAYSIZE( layoutModel );
	    HRESULT hr = vertexShader_light.Initialize( device, L"res\\shaders\\Model.fx", layoutModel, numElements );
		COM_ERROR_IF_FAILED( hr, "Failed to create light vertex shader!" );
	    hr = pixelShader_light.Initialize( device, L"res\\shaders\\Model.fx" );
		COM_ERROR_IF_FAILED( hr, "Failed to create light pixel shader!" );
	    hr = pixelShader_noLight.Initialize( device, L"res\\shaders\\Model_NoLight.fx" );
		COM_ERROR_IF_FAILED( hr, "Failed to create no light pixel shader!" );

        /*   POST-PROCESSING   */
        D3D11_INPUT_ELEMENT_DESC layoutFull[] = {
		    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	    };
        numElements = ARRAYSIZE( layoutFull );
	    hr = vertexShader_full.Initialize( device, L"res\\shaders\\Fullscreen.fx", layoutFull, numElements );
		COM_ERROR_IF_FAILED( hr, "Failed to create fullscreen vertex shader!" );
	    hr = pixelShader_full.Initialize( device, L"res\\shaders\\Fullscreen.fx" );
		COM_ERROR_IF_FAILED( hr, "Failed to create fullscreen pixel shader!" );

        /*   SPRITES   */
        D3D11_INPUT_ELEMENT_DESC layoutSprite[] = {
		    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	    };
	    numElements = ARRAYSIZE( layoutSprite );
	    hr = vertexShader_2D.Initialize( device, L"res\\shaders\\Sprite.fx", layoutSprite, numElements );
		COM_ERROR_IF_FAILED( hr, "Failed to create 2D vertex shader!" );
	    hr = pixelShader_2D.Initialize( device, L"res\\shaders\\Sprite.fx" );
		COM_ERROR_IF_FAILED( hr, "Failed to create 2D pixel shader!" );
        hr = pixelShader_2D_discard.Initialize( device, L"res\\shaders\\Sprite_Discard.fx" );
		COM_ERROR_IF_FAILED( hr, "Failed to create 2D discard pixel shader!" );
    }
    catch ( COMException& exception )
    {
        ErrorLogger::Log( exception );
        return false;
    }
	
	return true;
}

bool Graphics::InitializeScene()
{
    try
    {
        /*   MODELS   */
		if ( !nanosuit.Initialize( "res\\models\\nanosuit\\nanosuit.obj",
			device.Get(), context.Get(), cb_vs_matrix ) )
			return false;

		if ( !light.Initialize( device.Get(), context.Get(), cb_vs_matrix ) )
			return false;

        /*   SPRITES   */
        if ( !circle.Initialize( device.Get(), context.Get(), 256, 256, "res\\textures\\circle.png", cb_vs_matrix_2d ) )
            return false;
        circle.SetPosition( XMFLOAT3( windowWidth / 2 - circle.GetWidth() / 2, windowHeight / 2 - circle.GetHeight() / 2, 0 ) );

        if ( !square.Initialize( device.Get(), context.Get(), 256, 256, "res\\textures\\purpleheart.png", cb_vs_matrix_2d ) )
            return false;
        square.SetPosition( XMFLOAT3( windowWidth / 2 - square.GetWidth() / 2, windowHeight / 2 - square.GetHeight() / 2, 0 ) );

        /*   OBJECTS   */
        camera2D.SetProjectionValues( static_cast<float>( windowWidth ), static_cast<float>( windowHeight ), 0.0f, 1.0f );

        camera3D.SetPosition( XMFLOAT3( 0.0f, 9.0f, -15.0f ) );
	    camera3D.SetProjectionValues( 70.0f,
		    static_cast<float>( windowWidth ) / static_cast<float>( windowHeight ),
		    0.1f, 1000.0f );

        XMVECTOR lightPosition = camera3D.GetPositionVector();
		lightPosition += camera3D.GetForwardVector() + XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f );
		light.SetPosition( lightPosition );
		light.SetRotation( camera3D.GetRotationFloat3() );    

        /*   VERTEX/INDEX   */
        HRESULT hr = vertexBufferCube.Initialize( device.Get(),
            VTX::verticesCube_PosTexNrm, ARRAYSIZE( VTX::verticesCube_PosTexNrm ) );
        COM_ERROR_IF_FAILED( hr, "Failed to create cube vertex buffer!" );
        hr = indexBufferCube.Initialize( device.Get(), IDX::indicesLightCube, ARRAYSIZE( IDX::indicesLightCube ) );
        COM_ERROR_IF_FAILED( hr, "Failed to create cube index buffer!" );

        /*   TEXTURES   */
        hr = vertexBufferFullscreen.Initialize( device.Get(),
            VTX::verticesFullscreen, ARRAYSIZE( VTX::verticesFullscreen ) );
        COM_ERROR_IF_FAILED( hr, "Failed to create fullscreen quad vertex buffer!" );
        hr = indexBufferFullscreen.Initialize( device.Get(), IDX::indicesFullscreen, ARRAYSIZE( IDX::indicesFullscreen ) );
        COM_ERROR_IF_FAILED( hr, "Failed to create fullscreen quad index buffer!" );

        hr = DirectX::CreateWICTextureFromFile(
            device.Get(),
            L"res\\textures\\CrashBox.png",
            nullptr,
            boxTexture.GetAddressOf() );
        COM_ERROR_IF_FAILED( hr, "Failed to create WIC texture from file!" );

        /*   CONSTANT BUFFERS   */
        hr = cb_vs_fog.Initialize( device.Get(), context.Get() );
		COM_ERROR_IF_FAILED( hr, "Failed to initialize 'cb_vs_fog' Constant Buffer!" );
        cb_vs_fog.data.fogColor = XMFLOAT3( 0.2f, 0.2f, 0.2f );
        cb_vs_fog.data.fogStart = 10.0f;
        cb_vs_fog.data.fogEnd = 50.0f;
        cb_vs_fog.data.fogEnable = false;

        hr = cb_vs_matrix.Initialize( device.Get(), context.Get() );
		COM_ERROR_IF_FAILED( hr, "Failed to initialize 'cb_vs_matrix' Constant Buffer!" );

        hr = cb_vs_matrix_2d.Initialize( device.Get(), context.Get() );
		COM_ERROR_IF_FAILED( hr, "Failed to initialize 'cb_vs_matrix_2d' Constant Buffer!" );

        hr = cb_vs_fullscreen.Initialize( device.Get(), context.Get() );
		COM_ERROR_IF_FAILED( hr, "Failed to initialize 'cb_vs_fullscreen' Constant Buffer!" );

		hr = cb_ps_light.Initialize( device.Get(), context.Get() );
		COM_ERROR_IF_FAILED( hr, "Failed to initialize 'cb_ps_pixelshader' Constant Buffer!" );
    }
    catch ( COMException& exception )
    {
        ErrorLogger::Log( exception );
        return false;
    }
    return true;
}