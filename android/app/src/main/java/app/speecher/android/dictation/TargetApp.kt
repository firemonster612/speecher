package app.speecher.android.dictation

import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.text.InputType
import android.view.inputmethod.EditorInfo
import app.speecher.protocol.AppCategory
import app.speecher.protocol.CleanupStrength
import app.speecher.protocol.NearbyText
import app.speecher.protocol.RefinementContext
import app.speecher.protocol.resolveRefinementContext

/** The app and field the IME is typing into, read once per field in onStartInput. */
data class TargetApp(
    val packageName: String,
    val label: String,
    val category: AppCategory?,
    /** A password field: its text never leaves the device, as on the desktop. */
    val secure: Boolean,
    /** The field's kind, from its input type; see [controlRole]. */
    val role: String = "",
    /** The field's placeholder, such as "Search" or "Message". */
    val hint: String = "",
)

/** How much text on each side of the caret the refiner sees, the desktop's limit. */
private const val CONTEXT_CHARACTERS = 240

private val passwordTypes =
    setOf(
        InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD,
        InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD,
        InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_WEB_PASSWORD,
        InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_VARIATION_PASSWORD,
    )

private val textRoles =
    mapOf(
        InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS to "email address",
        InputType.TYPE_TEXT_VARIATION_EMAIL_SUBJECT to "email subject",
        InputType.TYPE_TEXT_VARIATION_SHORT_MESSAGE to "short message",
        InputType.TYPE_TEXT_VARIATION_LONG_MESSAGE to "long message",
        InputType.TYPE_TEXT_VARIATION_PERSON_NAME to "person name",
        InputType.TYPE_TEXT_VARIATION_POSTAL_ADDRESS to "postal address",
        InputType.TYPE_TEXT_VARIATION_URI to "url",
        InputType.TYPE_TEXT_VARIATION_FILTER to "filter",
    )

/**
 * The field's kind in words, the Android counterpart of the accessibility role the desktop sends as
 * control_role: a text variation when the app names one, otherwise the input class.
 */
fun controlRole(inputType: Int): String =
    when (inputType and InputType.TYPE_MASK_CLASS) {
        InputType.TYPE_CLASS_TEXT ->
            textRoles[inputType and InputType.TYPE_MASK_VARIATION]
                ?: if (inputType and InputType.TYPE_TEXT_FLAG_MULTI_LINE != 0) "multi-line text"
                else "text"
        InputType.TYPE_CLASS_NUMBER -> "number"
        InputType.TYPE_CLASS_PHONE -> "phone number"
        InputType.TYPE_CLASS_DATETIME -> "date and time"
        else -> ""
    }

/**
 * An IME can see the app it serves without a `<queries>` entry. Only the Productivity category maps
 * onto a desktop category (Office); the rest classify by name as the desktop does. Productivity
 * also holds note and to-do apps, which the desktop would leave General; they take Office and the
 * Work profile too, a deliberate trade for recognising office suites that no name rule catches.
 */
fun targetApp(editor: EditorInfo, packages: PackageManager): TargetApp {
    val name = editor.packageName.orEmpty()
    val info =
        try {
            packages.getApplicationInfo(name, 0)
        } catch (_: PackageManager.NameNotFoundException) {
            null
        }
    return TargetApp(
        name,
        info?.let { packages.getApplicationLabel(it).toString() }.orEmpty(),
        if (info?.category == ApplicationInfo.CATEGORY_PRODUCTIVITY) AppCategory.Office else null,
        editor.inputType and (InputType.TYPE_MASK_CLASS or InputType.TYPE_MASK_VARIATION) in
            passwordTypes,
        controlRole(editor.inputType),
        editor.hintText?.toString().orEmpty(),
    )
}

/**
 * Splits InputConnection.getSurroundingText around its selection. [offset] is where [text] starts
 * in the field, -1 when the editor does not say, which leaves the selection offsets unknown. Null
 * when the editor reports a selection outside [text]: the context is optional, the refinement is
 * not.
 */
fun nearbyText(
    text: CharSequence,
    selectionStart: Int,
    selectionEnd: Int,
    offset: Int,
): NearbyText? {
    if (selectionStart !in 0..selectionEnd || selectionEnd > text.length) return null
    return NearbyText(
        text.substring(0, selectionStart),
        text.substring(selectionEnd),
        if (offset >= 0) offset + selectionStart else -1,
        if (offset >= 0) offset + selectionEnd else -1,
    )
}

/**
 * The target's profile, cleanup and tone, as TranscriptPipeline resolves them. The app's identity
 * and the field's kind are always sent, as on the desktop. With context off or in a password field
 * nothing else about the field or screen is sent, and the text around the caret is not read at all;
 * nor is it read when the profile does no cleanup, since nothing is refined. [screen] and
 * [screenshotJpeg] are what the chip captured, only if the user opted in.
 */
fun refinementContext(
    settings: SpeecherSettings,
    target: TargetApp?,
    screen: ScreenCapture?,
    screenshotJpeg: String?,
    surroundingText: (Int) -> NearbyText?,
): RefinementContext {
    val includeText = settings.useTargetContext && target != null && !target.secure
    val context =
        resolveRefinementContext(
                target?.packageName.orEmpty(),
                target?.label.orEmpty(),
                target?.category,
                null,
                settings.defaultWritingProfile,
                settings.writingProfiles,
            )
            .copy(controlRole = target?.role.orEmpty())
    if (!includeText || context.style == CleanupStrength.None) return context
    return context.copy(
        nearbyText = surroundingText(CONTEXT_CHARACTERS) ?: NearbyText(),
        fieldHint = target.hint,
        // The desktop sends the window title regardless; here it comes from the screen text the
        // user opted into, so it stays behind the same switches.
        windowTitle = screen?.title.orEmpty(),
        screenText = screen?.text.orEmpty(),
        screenshotJpeg = screenshotJpeg,
    )
}
