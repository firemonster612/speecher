#pragma once

#include "core/Target.h"
#include "dictation/DictationTypes.h"
#include "dictation/SelectionEditPresentation.h"
#include "dictation/TranscriptPipeline.h"

#include <optional>

namespace speecher {

// The selection edit a Dictation Session holds for review, and whether a
// follow-up instruction for it is being dictated. A follow-up dictates on top
// of the revision under review and goes back to it when it comes to nothing.
class EditReview {
public:
    // A revision of the selection: the edit, what was said for it, and the
    // session it came from.
    struct Revision {
        QString edit;
        QStringList instructions;
        Target target;
        AppSettings settings;
        TranscriptPipelineResult pipeline;
        // What the receipt reports for this revision.
        QString speechWarning;
        ProviderHistory providerHistory;
        // What Insights records for it, across every instruction.
        int spokenWords = 0;
        int listeningMs = 0;
    };

    // A revision is under review, a follow-up to it dictated or not.
    bool isUp() const;
    bool followingUp() const;
    // The revision under review; only while one is up.
    const Revision &revision() const;

    // Puts revision under review. A follow-up's adds to the revision before
    // it: its instructions, words and listening time follow those before, and
    // the providers it ran on follow theirs.
    void revise(Revision revision);
    void startFollowUp();
    // The follow-up came to nothing: the revision before stays.
    void endFollowUp();
    void end();

    // The keys the review names; true when they changed.
    bool setKeys(const ReviewKeys &keys);
    const ReviewKeys &keys() const;
    // The revision under review as the popup shows it.
    SelectionEditReview presentation() const;

private:
    std::optional<Revision> m_revision;
    bool m_followingUp = false;
    ReviewKeys m_keys;
};

} // namespace speecher
