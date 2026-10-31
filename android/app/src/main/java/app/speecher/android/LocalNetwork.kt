package app.speecher.android

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import androidx.activity.result.ActivityResultLauncher
import java.net.ConnectException
import java.net.Inet6Address
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Proxy
import java.net.Socket
import java.net.SocketAddress
import java.net.UnknownHostException
import javax.net.SocketFactory
import okhttp3.HttpUrl.Companion.toHttpUrlOrNull
import okhttp3.OkHttpClient

// From Android 17 a socket to an address on the person's network fails until Speecher holds
// ACCESS_LOCAL_NETWORK: for the computer import and for a speech Custom Endpoint there.

/** Whether Android keeps Speecher off the local network until the person allows it. */
fun lacksLocalNetwork(context: Context): Boolean =
    Build.VERSION.SDK_INT >= Build.VERSION_CODES.CINNAMON_BUN &&
        context.checkSelfPermission(Manifest.permission.ACCESS_LOCAL_NETWORK) !=
            PackageManager.PERMISSION_GRANTED

/**
 * Whether [server]'s host is on the local network: a private, link-local or unique local address,
 * or an mDNS `.local` name. Looks other names up, so call it off the main thread; a name that does
 * not resolve counts as not local.
 */
fun onLocalNetwork(server: String): Boolean {
    val host = server.toHttpUrlOrNull()?.host ?: return false
    if (host.endsWith(".local")) return true
    val addresses =
        try {
            InetAddress.getAllByName(host)
        } catch (_: UnknownHostException) {
            return false
        }
    return addresses.any { it.isLocal }
}

private val InetAddress.isLocal: Boolean
    get() =
        isSiteLocalAddress ||
            isLinkLocalAddress ||
            (this is Inet6Address && address[0].toInt() and 0xfe == 0xfc)

/** Where an API key may go over plain http: the local network or this device. */
private val InetAddress.mayTakeKeyInClear: Boolean
    get() = isLocal || isLoopbackAddress

/**
 * Whether sending [apiKey] to [server] would carry it in clear text beyond the local network: plain
 * http to a host with an address neither on the local network nor this device. The desktop sends it
 * anyway; a phone moves between networks it does not control. Looks the name up, so call it off the
 * main thread; [keyedEndpointHttp] holds the connection itself to the rule.
 */
fun exposesApiKey(server: String, apiKey: String): Boolean {
    if (!sendsKeyInClear(server, apiKey)) return false
    return try {
        InetAddress.getAllByName(server.toHttpUrlOrNull()!!.host).any { !it.mayTakeKeyInClear }
    } catch (_: UnknownHostException) {
        // The request will not connect either.
        false
    }
}

/**
 * [http] for requests carrying [apiKey] to [server]. Over plain http they connect, with no proxy,
 * only to an address on the local network or this device, whatever a name resolves to then, and
 * fail with [KEY_NEEDS_LOCAL_NETWORK] before sending anything elsewhere.
 */
fun keyedEndpointHttp(http: OkHttpClient, server: String, apiKey: String): OkHttpClient =
    if (!sendsKeyInClear(server, apiKey)) http
    else http.newBuilder().proxy(Proxy.NO_PROXY).socketFactory(LocalSockets).build()

const val KEY_NEEDS_LOCAL_NETWORK =
    "Speecher sends the API key over plain http only to a server on your network. Use https, or " +
        "remove the key."

private fun sendsKeyInClear(server: String, apiKey: String): Boolean =
    apiKey.isNotEmpty() && server.toHttpUrlOrNull()?.isHttps == false

/** Sockets that connect only where [mayTakeKeyInClear]. OkHttp opens its sockets unconnected. */
private object LocalSockets : SocketFactory() {
    override fun createSocket(): Socket =
        object : Socket() {
            override fun connect(endpoint: SocketAddress, timeout: Int) {
                val address = (endpoint as? InetSocketAddress)?.address
                if (address?.mayTakeKeyInClear != true)
                    throw ConnectException(KEY_NEEDS_LOCAL_NETWORK)
                super.connect(endpoint, timeout)
            }
        }

    override fun createSocket(host: String, port: Int) = unconnectedOnly()

    override fun createSocket(host: String, port: Int, localHost: InetAddress, localPort: Int) =
        unconnectedOnly()

    override fun createSocket(host: InetAddress, port: Int) = unconnectedOnly()

    override fun createSocket(
        address: InetAddress,
        port: Int,
        localAddress: InetAddress,
        localPort: Int,
    ) = unconnectedOnly()

    private fun unconnectedOnly(): Nothing =
        throw UnsupportedOperationException("Only unconnected sockets, which connect checks")
}

/**
 * True when Speecher may reach the local network. Otherwise asks through [prompt], or, when the
 * person [refused] it before and Android no longer shows its prompt (it stops after a second
 * refusal), opens Speecher's settings, where Nearby devices is under Permissions.
 */
fun Activity.mayUseLocalNetwork(prompt: ActivityResultLauncher<String>, refused: Boolean): Boolean {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.CINNAMON_BUN) return true
    val permission = Manifest.permission.ACCESS_LOCAL_NETWORK
    when {
        checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED -> return true
        refused && !shouldShowRequestPermissionRationale(permission) -> openAppSettings()
        else -> prompt.launch(permission)
    }
    return false
}
