package app.speecher.android.update

/** Where checking for and installing an update stands, for Home's card and the Settings row. */
sealed interface UpdateState {
    /** The update this state is about, if any. */
    val update: ApkUpdate?
        get() = null

    /** Nothing checked yet this version. */
    data object Idle : UpdateState

    data object Checking : UpdateState

    data object UpToDate : UpdateState

    data object CheckFailed : UpdateState

    data class Available(override val update: ApkUpdate) : UpdateState

    /** [percent] is null while the download's size is unknown. */
    data class Downloading(override val update: ApkUpdate, val percent: Int?) : UpdateState

    /** Downloaded and held, because replacing the app would end the running dictation. */
    data class WaitingForDictation(override val update: ApkUpdate) : UpdateState

    /** [manualInstall] when only installing from the release page can work. */
    data class InstallFailed(
        override val update: ApkUpdate,
        val message: String,
        val manualInstall: Boolean = false,
    ) : UpdateState
}

/** Whether an update is on its way in, which a check must not replace. */
val UpdateState.installing: Boolean
    get() = this is UpdateState.Downloading || this is UpdateState.WaitingForDictation
