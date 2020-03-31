#include "net/socket/socket_wrapper.h"
#include "extension/strings/string_util.h"
#include "tnet_utils.h"
#include "tsk_debug.h"
#include "tnet_transport.h"
#include "tnet_proxydetect.h"
#include "net/nim_net_util.h"

NET_BEGIN_DECLS

#if defined(OS_WIN)
#define WOULD_BLOCK(status) ((status) == TNET_ERROR_WOULDBLOCK || (status) == TNET_ERROR_ISCONN || (status) == TNET_ERROR_INTR || (status) == TNET_ERROR_INPROGRESS)
#else
#define WOULD_BLOCK(status) ((status) == TNET_ERROR_WOULDBLOCK || (status) == TNET_ERROR_ISCONN || (status) == TNET_ERROR_INPROGRESS || (status) == TNET_ERROR_EAGAIN)
#endif

bool would_block(bool connecting)
{
	int err = tnet_geterrno();
#if defined(DEBUG) || defined(_DEBUG)
	if (err == TNET_ERROR_WOULDBLOCK ||
		err == TNET_ERROR_INPROGRESS ||
		((err == 0) && (true == connecting)) ||/*解决browser manager程序管理软件导致socket连接建立失败的问题。modified by HarrisonFeng, 2012-10-11*/
		err == 2)/*Because I reset socket receive buffer size in my program. And in debug mode, system will not alloc system buffer for the socket if no data arrive
				 so it may return value 2 means file not ready or exist. If the data already arrive, even one and only one byte, system will alloc the buffer size
				 as I reset, and then it will go well. In release mode, buffer will immediately alloc after reset buffer size of socket, so it will go well.
				 added by HarrisonFeng, 2011-08-10*/
#else
	if (err == TNET_ERROR_WOULDBLOCK ||
		err == TNET_ERROR_INPROGRESS ||
		((err == 0) && (true == connecting)))/*解决browser manager程序管理软件导致socket连接建立失败的问题。modified by HarrisonFeng, 2012-10-11*/
#endif
	{
		return true;
	}
	//FCX_DEBUG_ERROR("[fcore::would_block] err_code:%d", err);
	return false;
}

int ConvertNetProxyType(ProxyType type)
{
	int tnet_proxy_type = tnet_proxy_type_none;
	switch (type)
	{
	case kProxyNone:
		tnet_proxy_type = tnet_proxy_type_none;
	break;
	case kProxyHttp11:
		tnet_proxy_type = tnet_proxy_type_http;
	break;
	case kProxySocks4:
		tnet_proxy_type = tnet_proxy_type_socks4;
	break;
	case kProxySocks4a:
		tnet_proxy_type = tnet_proxy_type_socks4a;
	break;
	case kProxySocks5:
		tnet_proxy_type = tnet_proxy_type_socks5;
	break;
	default:
		tnet_proxy_type = tnet_proxy_type_none;
	break;
	}

	return tnet_proxy_type;

}

static tnet_proxyinfo_t* createProxyInfo(const ProxyInfo& config)
{

	auto proxyInfo = tnet_proxyinfo_create();
	proxyInfo->autodetect = tsk_false;
	proxyInfo->port = config.port_;
	proxyInfo->type = (tnet_proxy_type_t)ConvertNetProxyType(config.type_);

	if (!config.host_.empty())
	{
		int length = config.host_.length();
		char* hostname = new char[length + 1];
		memset(hostname, 0, sizeof(char)*(length + 1));
		memcpy(hostname, config.host_.c_str(), length);
		proxyInfo->hostname = hostname;
	}

	if (!config.user_.empty())
	{
		int length = config.user_.length();
		char* username = new char[length + 1];
		memset(username, 0, sizeof(char)*(length + 1));
		memcpy(username, config.user_.c_str(), length);
		proxyInfo->username = username;
	}

	if (!config.password_.empty())
	{
		int length = config.password_.length();
		char* pass_word = new char[length + 1];
		memset(pass_word, 0, sizeof(char)*(length + 1));
		memcpy(pass_word, config.password_.c_str(), length);
		proxyInfo->password = pass_word;
	}

	return proxyInfo;
}


namespace internal{

static int OnNetLogCallback(const void* arg, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	std::string msg;
	NS_EXTENSION::StringAppendV(msg, fmt, ap);
	va_end(ap);

	return 0;
}

TcpClientImpl::TcpClientImpl() : handler_(nullptr), socket_handle_(nullptr), fd_(TNET_INVALID_FD), is_connected_(false)
{
}

TcpClientImpl::~TcpClientImpl()
{
	Close();
	handler_ = nullptr;
}

void TcpClientImpl::SetHandler(TcpClientHandler *handler)
{
	handler_ = handler;
}

void TcpClientImpl::SetProxy(ProxyType type, const std::string& host, int port, const std::string& user, const std::string& password)
{
	proxyinfo_.host_ = host;
	proxyinfo_.port_ = port;
	proxyinfo_.user_ = user;
	proxyinfo_.password_ = password;
	proxyinfo_.type_ = type;

}
int TcpClientImpl::OnTcpCallback(const tnet_transport_event_t* e)
{
	auto weak_tcp_client = (std::weak_ptr<TcpClientImpl>*)(e->callback_data);
	auto tcp_client = ((weak_tcp_client != nullptr && !weak_tcp_client->expired()) ? weak_tcp_client->lock() : nullptr);
	if (nullptr == tcp_client)
	{
		delete weak_tcp_client;
		weak_tcp_client = nullptr;
		return -1;
	}
	switch (e->type) {
	case event_data:
	{
		tcp_client->OnReceive(NO_ERROR, (const char*)e->data, e->size);
	}
	break;
	case event_error:
	case event_closed:
	{
		tcp_client->OnClose(NO_ERROR);
		delete weak_tcp_client;
		weak_tcp_client = nullptr;
	}
	break;
	case event_connected:
	{
		tcp_client->OnConnect(ERROR_SUCCESS);
	}
	break;
	default:
	{
		break;
	}
	}
	return 0;
}
tnet_socket_type_e TcpClientImpl::CalcSocketType(const std::string& host, std::string& description)
{
	tnet_socket_type_e socket_type = tnet_socket_type_tcp_ipv4;
	description = "TCP/IPV4 TRANSPORT";
	net::AddressFamily addr_family;
	if (NimNetUtil::GetAddressFamily(host, addr_family))
	{
		if (addr_family == ADDRESS_FAMILY_IPV6)
		{
			socket_type = tnet_socket_type_tcp_ipv6;
			description = "TCP/IPV6 TRANSPORT";
		}
	}	
	return socket_type;
}
bool TcpClientImpl::Init(const std::string& host, int port)
{
	InitLog();	
	std::string description;
	auto socket_type = CalcSocketType(host, description);
	socket_handle_ = tnet_transport_create(TNET_SOCKET_HOST_ANY, TNET_SOCKET_PORT_ANY, socket_type, description.c_str());
	if (nullptr == socket_handle_)
	{
		return false;
	}
	// Set our callback function
	tnet_transport_set_callback(socket_handle_, &TcpClientImpl::OnTcpCallback, new std::weak_ptr<TcpClientImpl>(shared_from_this()));
	// Set proxy
	if (proxyinfo_.Valid())
	{
		((tnet_transport_t*)(socket_handle_))->proxy.auto_detect = tsk_false;
		((tnet_transport_t*)(socket_handle_))->proxy.info = createProxyInfo(proxyinfo_);
	}

	if (tnet_transport_start(socket_handle_))
	{		
		return false;
	}

	//Connect to server. tnet_socket_type_t type = tnet_socket_type_tcp_ipv4;
	if (((fd_ = tnet_transport_connectto_2(socket_handle_, host.c_str(), port)) == TNET_INVALID_FD) 
		&&	!would_block(true))//连接过程中判断是否阻塞
	{
		return false;
	}
	return true;
}

void TcpClientImpl::InitLog()
{
	tsk_debug_set_level(DEBUG_LEVEL_INFO);
	tsk_debug_set_info_cb(OnNetLogCallback);
	tsk_debug_set_error_cb(OnNetLogCallback);
	tsk_debug_set_warn_cb(OnNetLogCallback);
	tsk_debug_set_fatal_cb(OnNetLogCallback);
}

int	TcpClientImpl::Write(const void *data, size_t size)
{
	if (nullptr == socket_handle_)
	{
		//TODO(litianyi) 原因 20160427，最近可以追叙的bug是2.1.0版本 NIM-3713 PC端网络断开时会有一定几率导致崩溃，时间为3月28日，需要追查之前的代码改动
		return SOCKET_ERROR;
	}
	int n = tnet_transport_send(socket_handle_, fd_, data, (uint32_t)size);//发送失败时返回0
	if (0 == n)
	{
		//如果是阻塞，则不返回SOCKET_ERROR错误，否则返回SOCKET_ERROR错误！modified by HarrisonFeng, 2013.11.1
		if (!would_block())
		{
			n = SOCKET_ERROR;//返回SOCKET_ERROR错误，上层才会判定是发送失败，否则都认为发送成功！！！
		}
	}
	return n;
}

int	TcpClientImpl::Read(const void *data, size_t size)
{
	int n = (int)size;//接收失败时返回0
	if (0 == n)
	{
		//如果是阻塞，则不返回SOCKET_ERROR错误，否则返回SOCKET_ERROR错误！modified by HarrisonFeng, 2013.11.1
		if (false == would_block())
		{
			n = SOCKET_ERROR;//返回SOCKET_ERROR错误，上层才会判定是发送失败，否则都认为发送成功！！！
		}
	}
	return n;
}

void TcpClientImpl::Close()
{
	if (socket_handle_ != nullptr)
		tnet_transport_set_callback(socket_handle_, nullptr, nullptr);//将BizTcpClient作为回调数据传到回调函数里，方便接口访问。
	TSK_OBJECT_SAFE_FREE(socket_handle_);//该释放操作会执行close fd，无需在上层显示关闭
	fd_ = TNET_INVALID_FD;//上层定义的句柄需要置为无效。modified by HarrisonFeng, 2014.9.23
}

bool TcpClientImpl::IsConnected()
{
	return is_connected_;
}

void TcpClientImpl::OnClose(int error_code)
{
	is_connected_ = false;

	if (handler_)
		handler_->OnClose(error_code);
}

void TcpClientImpl::OnConnect(int error_code)
{
	is_connected_ = (error_code == ERROR_SUCCESS);

	if (handler_)
		handler_->OnConnect(error_code);
}

void TcpClientImpl::OnAccept(int error_code)
{
}

void TcpClientImpl::OnReceive(int error_code, const void *data, size_t size)
{
	if (handler_)
		handler_->OnReceive(error_code, data, size);
}

void TcpClientImpl::OnSend(int error_code)
{
	if (handler_)
		handler_->OnSend(error_code);
}

////////////////////////////////////////////////////////////////////////////////////////////
UDPClientImpl::UDPClientImpl() : handler_(nullptr), socket_handle_(nullptr),fd_(TNET_INVALID_FD), is_connected_(false)
{
}

UDPClientImpl::~UDPClientImpl()
{
	Close();
	handler_ = nullptr;
}

void UDPClientImpl::SetHandler(UdpClientHandler *handler)
{
	handler_ = handler;
}

int UDPClientImpl::OnUDPCallback(const tnet_transport_event_t* e)
{
	auto weak_udp_client = (std::weak_ptr<UDPClientImpl>*)(e->callback_data);
	auto udp_client = weak_udp_client ? weak_udp_client->lock() : nullptr;
	if (nullptr == udp_client)
	{
		delete weak_udp_client;
		return -1;
	}
	switch (e->type) {
	case event_data:
	{
		//QLOG_PRO("--- UDP ---\n%s\n", (const char*)e->data);
		udp_client->OnReceive(NO_ERROR, (const char*)e->data, e->size);
	}
	break;
	case event_closed:
	{
		//QLOG_PRO(L"Socket Peer closed with fd = {0}") << e->local_fd;
		udp_client->OnClose(NO_ERROR);
	}
	break;
	case event_connected:
	{
		//QLOG_PRO(L"Socket Peer accepted/connected with fd = {0}") << e->local_fd;
		udp_client->OnConnect(ERROR_SUCCESS);

	}
	break;
	default:
	{
		break;
	}
	}
	return 0;
}

bool UDPClientImpl::Init(const std::string& host, int port)
{
	socket_handle_ = tnet_transport_create(TNET_SOCKET_HOST_ANY, TNET_SOCKET_PORT_ANY, tnet_socket_type_udp_ipv4, "UDP/IPV4 TRANSPORT");
	if (nullptr == socket_handle_)
	{
		return false;
	}
	// Set our callback function
	tnet_transport_set_callback(socket_handle_, &UDPClientImpl::OnUDPCallback, new std::weak_ptr<UDPClientImpl>(shared_from_this()));//将BizUdpClient作为回调数据传到回调函数里，方便接口访问。
	if (tnet_transport_start(socket_handle_))
	{
		return false;
	}

	//Connect to server. tnet_socket_type_t type = tnet_socket_type_tcp_ipv4;
	if (((fd_ = tnet_transport_connectto_2(socket_handle_, host.c_str(), port)) == TNET_INVALID_FD) &&
		!would_block(true))//连接过程中判断是否阻塞
	{
		return false;
	}

	if (tnet_sockfd_waitUntilWritable(fd_, TNET_CONNECT_TIMEOUT))
	{
		tnet_transport_remove_socket(socket_handle_, &fd_);
		return false;
	}

	return true;
}

int	UDPClientImpl::Write(const void *data, size_t size)
{
	int n = tnet_transport_send(socket_handle_, fd_, data, (tsk_size_t)size);//发送失败时返回0
	if (0 == n)
	{//如果是阻塞，则不返回SOCKET_ERROR错误，否则返回SOCKET_ERROR错误！modified by HarrisonFeng, 2013.11.1
		if (false == would_block())
		{
			n = SOCKET_ERROR;//返回SOCKET_ERROR错误，上层才会判定是发送失败，否则都认为发送成功！！！
		}
	}
	return n;
}

int	UDPClientImpl::Read(const void *data, size_t size)
{
	int n = (int)size;//接收失败时返回0
	if (0 == n)
	{//如果是阻塞，则不返回SOCKET_ERROR错误，否则返回SOCKET_ERROR错误！modified by HarrisonFeng, 2013.11.1
		if (false == would_block())
		{
			n = SOCKET_ERROR;//返回SOCKET_ERROR错误，上层才会判定是发送失败，否则都认为发送成功！！！
		}
	}
	return n;
}

void UDPClientImpl::Close()
{
	TSK_OBJECT_SAFE_FREE(socket_handle_);//该释放操作会执行close fd，无需在上层显示关闭
	fd_ = TNET_INVALID_FD;//上层定义的句柄需要置为无效。modified by HarrisonFeng, 2014.9.23
}

bool UDPClientImpl::IsConnected()
{
	return is_connected_;
}

void UDPClientImpl::OnClose(int error_code)
{
	is_connected_ = false;

	if (handler_)
		handler_->OnClose(error_code);
}

void UDPClientImpl::OnConnect(int error_code)
{
	is_connected_ = (error_code == ERROR_SUCCESS);

	if (handler_)
		handler_->OnConnect(error_code);
}

void UDPClientImpl::OnAccept(int error_code)
{
}

void UDPClientImpl::OnReceive(int error_code, const void *data, size_t size)
{
	if (handler_)
		handler_->OnReceive(error_code, data, size);
}

void UDPClientImpl::OnSend(int error_code)
{
}


}
NET_END_DECLS
