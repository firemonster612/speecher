package app.speecher.android.dictation

import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.text.InputType
import android.view.inputmethod.EditorInfo
import app.speecher.protocol.AppCategory
import app.speecher.protocol.RefinementContext
import app.speecher.protocol.resolveRefinementContext

/** The app and field the IME is typing into, read once per field in onStartInput. */
data class TargetApp(
    val packageName: String,
    val label: String,
    val category: AppCategory?,
    /** A password field: its text never leaves the device, as on the desktop. */
    val secure: Boolean,
)

/** How much text before the caret the refiner sees, the desktop's limit. */
private const val CONTEXT_CHARACTERS = 240

private val passwordTypes =
    setOf(
        InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD,
        InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD,
        InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_WEB_PASSWORD,
        InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_VARIATION_PASSWORD,
    )

/**
 * An IME can see the app it serves without a `<queries>` entry. Only the Productivity category maps
 * onto a desktop category (Office); the rest classify by name as the desktop does.
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
    )
}

/**
 * The target's profile, cleanup and tone, as TranscriptPipeline resolves them. With context off or
 * in a password field the text before the caret is not read at all.
 */
fun refinementContext(
    settings: SpeecherSettings,
    target: TargetApp?,
    textBeforeCursor: (Int) -> CharSequence?,
): RefinementContext {
    val includeText = settings.useTargetContext && target != null && !target.secure
    return resolveRefinementContext(
        target?.packageName.orEmpty(),
        target?.label.orEmpty(),
        target?.category,
        if (includeText) textBeforeCursor(CONTEXT_CHARACTERS)?.toString().orEmpty() else null,
        settings.defaultWritingProfile,
        settings.writingProfiles,
    )
}
