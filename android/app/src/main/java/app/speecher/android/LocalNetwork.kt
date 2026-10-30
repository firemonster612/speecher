package app.speecher.android

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import androidx.activity.result.ActivityResultLauncher
import java.net.Inet6Address
import java.net.InetAddress
import java.net.UnknownHostException
import okhttp3.HttpUrl.Companion.toHttpUrlOrNull

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
    return addresses.any {
        it.isSiteLocalAddress ||
            it.isLinkLocalAddress ||
            (it is Inet6Address && it.address[0].toInt() and 0xfe == 0xfc)
    }
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
