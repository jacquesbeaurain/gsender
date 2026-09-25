import QtQuick
import GSender

// Vacuum Table: load a bundled program as the job, then the installer
// closes (to the Carve page).
WizardStepPage {
    id: page
    property bool grid: false
    WizardText {
        text: page.grid ? qsTr("Load the alignment grid pattern for your vacuum table. This will open the file in the main visualizer.")
                        : qsTr("Load the mounting-hole pattern for your vacuum table. This will open the file in the main visualizer.")
    }
    WizardAction {
        buttonName: "loadToVisualizer"
        text: qsTr("Load to Visualizer")
        runningText: qsTr("Loading...")
        onTriggered: {
            const loaded = page.grid ? page.model.loadVacuumGrid() : page.model.loadVacuumMounting(page.installer.vacuumSize)
            if (!loaded) {
                fail(page.grid ? qsTr("Unable to load the grid file. Please try again.")
                               : qsTr("Unable to load the mounting file. Please try again."))
                return
            }
            finish()
            page.complete = true
            page.finish()
        }
    }
}
