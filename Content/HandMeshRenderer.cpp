#include "pch.h"
#include "HandMeshRenderer.h"
#include "Common/DirectXHelper.h"

#include <sstream>

using namespace holo_winrt;
using namespace DirectX;
using namespace winrt::Windows::Foundation::Numerics;
using namespace winrt::Windows::UI::Input::Spatial;
using namespace winrt::Windows::Perception::Spatial;

HandMeshRenderer::HandMeshRenderer(std::shared_ptr<DX::DeviceResources> const& deviceResources, std::vector<winrt::Windows::Perception::People::HandMeshVertex> initVertices, std::vector<unsigned short> initIndices) :
	m_deviceResources(deviceResources)
{
	SetHandIndices(initIndices);
	CreateDeviceDependentResources(initVertices);
}

void HandMeshRenderer::SetModelConstantBuffer(winrt::Windows::Foundation::Numerics::float4x4 input_matrix)
{
	XMMATRIX matrix;
	matrix = XMLoadFloat4x4(&input_matrix);

	XMStoreFloat4x4(&m_modelConstantBufferData.model, XMMatrixTranspose(matrix) );
}

void HandMeshRenderer::TransformToStruct()
{
	std::vector<VertexPositionColor> new_arr;

	for (int i = 0; i < m_currentHandVert.size(); i++)
	{
		VertexPositionColor v( { XMFLOAT3(m_currentHandVert[i].Position.x, m_currentHandVert[i].Position.y, m_currentHandVert[i].Position.z),
								 XMFLOAT3(0.0f, 0.0f, 1.0f) });

		new_arr.push_back( v );
	}

	m_vertexBufferData.clear();
	m_vertexBufferData = new_arr;
	m_vertexBufferDataSize = new_arr.size();
}

//void HandMeshRenderer::SetHandIndices(std::vector<unsigned short> &indices)
//{
//	// resize std::vector first
//	// m_currentHandIndices.resize(indices.size());
//	m_currentHandIndices.clear();
//
//	// copy everything from input array
//	for (int i = 0; i < indices.size(); i++)
//	{
//		m_currentHandIndices.push_back(indices[i]);
//	}
//	// indices.clear();
//}

void HandMeshRenderer::Update(DX::StepTimer const& timer)
{
	if (!m_loadingComplete)
	{
		return;
	}

	// Now we need to update the vertex buffer

	const auto context = m_deviceResources->GetD3DDeviceContext();

	if (m_currentHandVert.size() > 0) {
		TransformToStruct();

		D3D11_MAPPED_SUBRESOURCE resource;
		ZeroMemory(&resource, sizeof(D3D11_MAPPED_SUBRESOURCE));
		context->Map(
			m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
		memcpy(resource.pData, m_vertexBufferData.data(), sizeof(VertexPositionColor) * static_cast<UINT>(m_vertexBufferDataSize));
		context->Unmap(m_vertexBuffer.Get(), 0);

	}

	// Update the model transform buffer for the hologram.
	context->UpdateSubresource(
		m_modelConstantBuffer.Get(),
		0,
		nullptr,
		&m_modelConstantBufferData,
		0,
		0
	);

	const FaceColorBuffer m_faceColorBufferData =
	{
		{
			{ 1.0f,1.0f,1.0f },
			{ 1.0f,0.0f,0.0f },
			{ 0.0f,1.0f,0.0f },
			{ 1.0f,1.0f,0.0f },
			{ 0.0f,0.0f,1.0f },
			{ 1.0f,0.0f,1.0f },
		}
	};

	context->UpdateSubresource(
		m_faceColorBuffer.Get(),
		0,
		nullptr,
		&m_faceColorBufferData,
		0,
		0
	);
}

void HandMeshRenderer::Render()
{
	if (!m_loadingComplete)
	{
		return;
	}

	const auto context = m_deviceResources->GetD3DDeviceContext();

	const UINT stride = sizeof(VertexPositionColor);
	const UINT offset = 0;

	context->IASetVertexBuffers(
		0,
		1,
		m_vertexBuffer.GetAddressOf(),
		&stride,
		&offset
	);

	context->IASetIndexBuffer(
		m_indexBuffer.Get(),
		DXGI_FORMAT_R16_UINT,
		0
	);

	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->IASetInputLayout(m_inputLayout.Get());

	context->VSSetShader(
		m_vertexShader.Get(),
		nullptr,
		0
	);

	context->VSSetConstantBuffers(
		0,
		1,
		m_modelConstantBuffer.GetAddressOf()
	);

	context->PSSetConstantBuffers(
		0,
		1,
		m_faceColorBuffer.GetAddressOf()
	);

	if (!m_usingVprtShaders)
	{
		context->GSSetShader(
			m_geometryShader.Get(),
			nullptr,
			0
		);
	}

	context->PSSetShader(
		m_pixelShader.Get(),
		nullptr,
		0
	);

	context->DrawIndexedInstanced(
		m_indexCount,
		2,
		0,
		0,
		0
	);
}

std::future<void> HandMeshRenderer::CreateDeviceDependentResources(std::vector<winrt::Windows::Perception::People::HandMeshVertex> receivedVertices)
{
	if (receivedVertices.size() < 1 || m_currentHandIndices.size() < 1) {
		return;
	}

	m_usingVprtShaders = m_deviceResources->GetDeviceSupportsVprt();

	// devices that do support d3d11_feature_d3d11_options3
	std::wstring vertexShaderFileName = m_usingVprtShaders ? L"ms-appx:///VprtVertexShader.cso" : L"ms-appx:///VertexShader.cso";

	std::vector<byte> vertexShaderFileData = co_await DX::ReadDataAsync(vertexShaderFileName);
	winrt::check_hresult(
		m_deviceResources->GetD3DDevice()->CreateVertexShader(
			vertexShaderFileData.data(),
			vertexShaderFileData.size(),
			nullptr,
			&m_vertexShader
		));

	constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 2> vertexDesc =
	{ {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	} };

	winrt::check_hresult(
		m_deviceResources->GetD3DDevice()->CreateInputLayout(
			vertexDesc.data(),
			static_cast<UINT>(vertexDesc.size()),
			vertexShaderFileData.data(),
			static_cast<UINT>(vertexShaderFileData.size()),
			&m_inputLayout
		));

	// After the pixel shader file is loaded, create the shader and constant buffer.
	std::vector<byte> pixelShaderFileData = co_await DX::ReadDataAsync(L"ms-appx:///PixelShader.cso");
	winrt::check_hresult(
		m_deviceResources->GetD3DDevice()->CreatePixelShader(
			pixelShaderFileData.data(),
			pixelShaderFileData.size(),
			nullptr,
			&m_pixelShader
		));

	const CD3D11_BUFFER_DESC constantBufferDesc(sizeof(ModelConstantBuffer), D3D11_BIND_CONSTANT_BUFFER);
	winrt::check_hresult(
		m_deviceResources->GetD3DDevice()->CreateBuffer(
			&constantBufferDesc,
			nullptr,
			&m_modelConstantBuffer
		));

	const CD3D11_BUFFER_DESC constantBufferDesc2(sizeof(FaceColorBuffer), D3D11_BIND_CONSTANT_BUFFER);
	winrt::check_hresult(
		m_deviceResources->GetD3DDevice()->CreateBuffer(
			&constantBufferDesc2,
			nullptr,
			&m_faceColorBuffer
		));

	// if not using vprtshaders -> use geomtryshader
	if (!m_usingVprtShaders)
	{
		// Load the pass-through geometry shader.
		std::vector<byte> geometryShaderFileData = co_await DX::ReadDataAsync(L"ms-appx:///GeometryShader.cso");

		// After the pass-through geometry shader file is loaded, create the shader.
		winrt::check_hresult(
			m_deviceResources->GetD3DDevice()->CreateGeometryShader(
				geometryShaderFileData.data(),
				geometryShaderFileData.size(),
				nullptr,
				&m_geometryShader
			));
	}

	// Load and transform HandMeshVertex std::vector to VertexPositionColor std::vector
	m_currentHandVert = receivedVertices;
	TransformToStruct();

	D3D11_SUBRESOURCE_DATA vertexBufferData = { 0 };
	vertexBufferData.pSysMem = m_vertexBufferData.data();
	vertexBufferData.SysMemPitch = 0;
	vertexBufferData.SysMemSlicePitch = 0;

	const CD3D11_BUFFER_DESC vertexBufferDesc(
		sizeof(VertexPositionColor) * static_cast<UINT>(m_vertexBufferData.size()),
		D3D11_BIND_VERTEX_BUFFER,
		D3D11_USAGE_DYNAMIC,
		D3D11_CPU_ACCESS_WRITE
	);
	winrt::check_hresult(
		m_deviceResources->GetD3DDevice()->CreateBuffer(
			&vertexBufferDesc,
			&vertexBufferData,
			&m_vertexBuffer
		));

	unsigned int i_count = static_cast<unsigned int>(m_currentHandIndices.size());
	m_indexCount = static_cast<unsigned int>(m_currentHandIndices.size());

	D3D11_SUBRESOURCE_DATA indexBufferData = { 0 };
	indexBufferData.pSysMem = m_currentHandIndices.data();
	indexBufferData.SysMemPitch = 0;
	indexBufferData.SysMemSlicePitch = 0;
	CD3D11_BUFFER_DESC indexBufferDesc(sizeof(unsigned short) * static_cast<UINT>(i_count), D3D11_BIND_INDEX_BUFFER);
	winrt::check_hresult(
		m_deviceResources->GetD3DDevice()->CreateBuffer(
			&indexBufferDesc,
			&indexBufferData,
			&m_indexBuffer
		));

	// Once the cube is loaded, the object is ready to be rendered.
	m_loadingComplete = true;
}

void HandMeshRenderer::ReleaseDeviceDependentResources()
{
	m_loadingComplete = false;
	m_usingVprtShaders = false;
	m_vertexShader.Reset();
	m_inputLayout.Reset();
	m_pixelShader.Reset();
	m_geometryShader.Reset();
	m_vertexBuffer.Reset();
	m_indexBuffer.Reset();
	m_modelConstantBuffer.Reset();
	m_faceColorBuffer.Reset();
}
