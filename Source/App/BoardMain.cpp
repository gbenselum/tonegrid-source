#include "Platform/AppPaths.h"
#include "Platform/LegacyProfileCleanup.h"
#include "Core/PresetLoadQueue.h"
#include "Core/ProcessingGraph.h"
#include "Modules/Play/PlayModule.h"
#include "Modules/Play/UI/PlayView.h"
#include "Modules/Jam/JamModule.h"
#include "Modules/Learn/LearnModule.h"
#include "Modules/Record/RecordModule.h"
#include "Shared/UI/Library/CabinetBrowserView.h"
#include "CabinetProcessor.h"
#include "Content/Amps/CaptureResolver.h"
#include "DeviceRateProcessor.h"
#include "Modules/Play/UI/PedalCard.h"
#include "Shared/UI/UiMotion138.h"
#include "Shared/UI/SoftTooltip.h"
#include "Modules/Play/UI/PedalMidiLearnView.h"
#include "Modules/Play/UI/PedalScene.h"
#include "Content/Presets/RigStore.h"
#include "MidiController.h"
#include "TonexAmpSource.h"
#include "Vst3Discovery.h"
#include "Vst3Loader.h"
#include "Vst3Validation.h"
#include "AmpReplacement.h"
#include "Vst3IntegrationTests.h"
#include "NamAmp.h"
#include "NamAmpIntegrationTests.h"
#include "AmpUi.h"
#include "AmpChassis.h"
#include "Effects/AmpEq/AdvancedEqControls.h"
#include "Modules/Jam/UI/JamView.h"
#include "Modules/Learn/UI/LearnView.h"
#include "../../Tests/Jam131BrowserProbe.h"
#include "../../Tests/Learn132BrowserProbe.h"
#include "../../Tests/RecordVideoBrowserProbe.h"
#include "../../Tests/Tone3000NativeBrowserProbe.h"
#include "Modules/Play/UI/LoopControls.h"
#include "UserPedalLibrary.h"
#include "Content/Presets/PresetShare.h"
#include "Shared/UI/CompactLogLabel.h"
#include "Shared/UI/StatusBarLabel.h"
#include "Shared/UI/Library/NamLibraryBrowser.h"
#include "Content/Amps/NamLibraryService.h"
#include "TunerControls.h"
#include "Modules/Play/UI/RecordingControls.h"
#include "AudioSettingsPanel.h"
#include "Shared/UI/StartupStatus.h"
#include "Shared/UI/ContentSettingsView.h"
#include "Shared/UI/PopupWindow.h"
#include "Shared/UI/ScalableBoard.h"
#include "DiagnosticView.h"
#include "BetaWelcome.h"
#include "CalibrationAction.h"
#include "Content/Presets/StarterPresets.h"
#include "GuitarCalibrationWizard.h"
#include "CalibrationWarnings.h"
#include "Content/Artwork/ThumbnailCache.h"
#include "Tone3000Browser.h"
#include "EffectsNodes.h"
#include "EffectsControls.h"
#include "Shared/UI/ToneGridLogo.h"
#include "PresetBrowser.h"
#include "MissingCapturesView.h"
#include "OnlineUpdates.h"
#include "Effects/StereoSpread/SpreadControls.h"
#include "Effects/Reverb/ReverbControls.h"
#include "BoardTests.h"
#include "IntegrationTests.h"
#include "../../Tests/NamBrowserLifetimeTests.h"
#include "../../Tests/TonexInitializationProbe.h"
#include "../../Tests/Tone3000Tests.h"
#include "../../Tests/RawInputTestDevice.h"
#include "../../Tests/AmpSourceTests.h"
#include "../../Tests/AudioRecovery126Devices.h"
#include <future>
using namespace juce;
class RigHost final : public Component, private MidiInputCallback, private Timer, private board::MidiController::Actions,private ChangeListener
{
public:
    explicit RigHost(const File& applicationData=tonegrid::paths::applicationDataRoot(),bool useHardware=true,bool useBundledAssetsForTest=false,bool liveBrowsersForValidation=false)
        : appPaths(tonegrid::paths::installed(applicationData)),hardwareEnabled(useHardware),liveValidationBrowsers(liveBrowsersForValidation),bundledBootstrapTest(useBundledAssetsForTest),rigStore(board::RigStore::forApplicationData(applicationData))
    {
        tonegrid::l10n::Language::instance().configure(appPaths.shippedTranslations(),appPaths.translations(),appPaths.languagePreference(),useHardware?SystemStats::getDisplayLanguage():String("en"));
        tonegrid::assets::configureSkinFolders137(appPaths);
        devices.addChangeListener(this);
        // Archived prototype TONEX rigs are not the factory User Presets. Keep
        // them untouched when this package supplies its own NAM starter bank.
        if(!(hardwareEnabled||bundledBootstrapTest)||!appPaths.starterPresets().getChildFile("manifest.json").existsAsFile())rigStore.migrateLegacyBank();
        auto recovery=rigStore.recover();if(recovery.failed())report(recovery.getErrorMessage());
        namLite=tonegrid::namquality::read(qualityPreference);
        calibrationSettings=tonegrid::calibration::read(calibrationPreference);
        welcomeOffered=tonegrid::welcome::dismissed(appPaths.betaWelcome());
        addAndMakeVisible(guitarCalibration);
        guitarCalibration.setComponentID("guitar-calibration");guitarCalibration.onClick=[this]{showGuitarCalibration();};
        guitarCalibration.nearby=[this]{return sections[4].expanded(7,4).contains(getMouseXYRelative());};
        midiDevice.setComponentID("midi-input");rescan.setComponentID("refresh-midi");rescan.setTitle("Refresh MIDI");tonegrid::l10n::tooltip(save,"Save this preset");tonegrid::l10n::tooltip(rescan,"Refresh MIDI devices");
        look.setColour(TextEditor::backgroundColourId,Colour(0xff1c303a));
        look.setColour(TextEditor::outlineColourId,Colour(0xff62747c));
        setLookAndFeel(&look);
        auto savedPath=bankDir.getChildFile("tonex-path.txt").loadFileAsString().trim();
        path.setText(savedPath.isEmpty()?amp::tonexDefaultPath():savedPath,false);
        path.setTooltip("TONEX VST3 path. Press Enter to load after correcting it.");
        path.onReturnKey=[this]{loadRig();}; path.onFocusLost=[this]{if(!ampSource||!ampSource->loaded())loadRig();};
        if(cacheStore.file().existsAsFile()){auto cached=cacheStore.read(pluginCatalog);if(cached.failed())report(cached.getErrorMessage());}
        vst3::Catalog preference;if(selectionStore.file().existsAsFile())
        {
            auto preferred=selectionStore.read(preference);
            if(preferred.wasOk()&&preference.plugins.size()==1)startupPlugin=preference.plugins[0];
            else report("Invalid startup VST3 preference. A local NAM will be used; select your plugin again from VST.");
        }
        namSourceButton.setComponentID("amp-source-nam");vstSourceButton.setComponentID("amp-source-vst");
        namSourceButton.onClick=[this]{changeSource(amp::SourceType::Nam);};vstSourceButton.onClick=[this]{changeSource(amp::SourceType::Vst3);};
        if(auto choice=XmlDocument::parse(sourcePreference))
        {
            if(choice->hasTagName("AmpSelection")&&choice->getIntAttribute("version")==1&&choice->getStringAttribute("type")=="NAM")
            {namamp::State saved;auto* source=choice->getChildByName("AmpSource");if(source&&namamp::decode(*source,saved)){startupNam=saved;displayedSource=amp::SourceType::Nam;}else report("Invalid NAM startup preference; select the model again.");}
            else if(!choice->hasTagName("AmpSelection")||choice->getIntAttribute("version")!=1||choice->getStringAttribute("type")!="VST3")report("Invalid amp source preference; existing VST3 preference retained.");
            if(choice->hasTagName("AmpSelection")&&choice->getIntAttribute("version")==1)
                if(auto* node=choice->getChildByName("OutputDSP")){tonegrid::output::Settings stored;if(tonegrid::output::decode(*node,stored))outputSettings=stored;else report("Invalid output DSP preference; safe defaults retained.");}
        }
        startupPresetPending=hardwareEnabled||bundledBootstrapTest;
        blockAutomaticVst=appPaths.user().getChildFile("Vst3/loading.xml").existsAsFile();
        if(blockAutomaticVst)
        {
            report("The previous plugin load did not finish. ToneGrid will start with NAM. Open VST to retry the plugin manually.");
            appPaths.user().getChildFile("Vst3/loading.xml").moveFileTo(appPaths.user().getChildFile("Vst3/last-failed-load.xml"));
        }
        starterBootstrapPending=startupPresetPending;
        if(starterBootstrapPending)displayedSource=amp::SourceType::Nam;
        namChoice.setComponentID("nam-amp");namChoice.setTextWhenNothingSelected("No local NAM / Refresh");namChoice.onChange=[this]{selectNam();};refreshNam.onClick=[this]{refreshNamLibrary();};
        namFolder.setComponentID("nam-folder");namFolder.setTitle("Choose local NAM capture");namFolder.setTooltip("Choose a capture from your NAM library, or refresh its folders.");namFolder.onClick=[this]{showNamChooser();};
        namBrowser.setFavoritesFile(appPaths.user().getChildFile("NAM/favorites.xml"));
        namBrowser.onClose=[this]{if(auto* dialog=namBrowserDialog.getComponent())dialog->closeButtonPressed();};
        namBrowser.onRefresh=[this]{refreshNamLibrary();};namBrowser.onSelect=[this](const String& relative){for(size_t i=0;i<namCatalog.entries.size();++i)if(namCatalog.entries[i].identity.file==relative){namChoice.setSelectedId((int)i+1,dontSendNotification);selectNam();const auto* current=dynamic_cast<const amp::NamAmp*>(ampSource.get());namBrowser.setImportStatus(current&&current->state().model.file==relative?String{}:pendingHead&&pendingHead->model.file==relative?String("Choose a cabinet to load this head."):"Could not load capture. Previous amp kept. Check the file or refresh the library.");return;}namBrowser.setImportStatus("Cannot load this capture. Refresh the library or check its NAM format.");report("NAM capture is incompatible or no longer indexed. Previous amp kept.");};
        namBrowser.onGetCaptures=[this]
        {
            // The browser component is owned by RigHost, so closing its non-owning
            // modal dialog here does not destroy the button running this callback.
            if(auto* dialog=namBrowserDialog.getComponent()){dialog->exitModalState(0);delete dialog;}
            tone3000Window.reset();tone3000Window=std::make_unique<tonegrid::t3k::Tone3000BrowserWindow>(namLibrary.folder(),appPaths.browserProfile(),
                [safe=Component::SafePointer<RigHost>(this)](const tonegrid::t3k::ImportResult& result){if(safe)safe->tone3000ImportFinished(result);},
                [safe=Component::SafePointer<RigHost>(this)]{if(safe)safe->showNamChooser();},
                tone3000OfflineTest?std::function<void(const String&)>{[this](const String& address){tone3000TestAuthorization=address;}}:std::function<void(const String&)>{},namBrowser.onlineGear());
            tone3000Window->start();
        };
        captureHeading.setText("NAM CAPTURE",dontSendNotification);captureHeading.setColour(Label::textColourId,tonegrid::ui::secondaryInk());namFilename.setColour(Label::textColourId,tonegrid::ui::ink());namFolderName.setColour(Label::textColourId,tonegrid::ui::metadataInk());
        namFilename.setComponentID("nam-filename");namFolderName.setComponentID("nam-group");thumbnail.setComponentID("nam-thumbnail");
        for(auto* knob:{&namGain,&namVolume}){knob->setLookAndFeel(&ampLook);knob->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);knob->setTextBoxStyle(Slider::TextBoxBelow,false,100,21);knob->setTextValueSuffix(" dB");knob->setNumDecimalPlacesToDisplay(1);knob->setColour(Slider::textBoxBackgroundColourId,Colours::transparentBlack);knob->setColour(Slider::textBoxOutlineColourId,Colours::transparentBlack);}
        for(auto* button:{&namSourceButton,&vstSourceButton,&spreadToggle,&spreadGear,&modToggle,&modGear,&delayToggle,&delayGear,&pitchGear,&reverbToggle,&reverbGear})button->setLookAndFeel(&ampLook);
        tonegrid::ampui::configure(namGain,-24,24);tonegrid::ampui::configure(namVolume,-24,24);namGain.setValue(0,dontSendNotification);namVolume.setValue(0,dontSendNotification);
        namGain.setTooltip("GAIN: input drive before NAM (-24 to +24 dB). Not the physical amplifier gain knob.");namVolume.setTooltip("VOLUME: output level after NAM (-24 to +24 dB).");
        namGain.onValueChange=namVolume.onValueChange=[this]{if(auto* source=dynamic_cast<amp::NamAmp*>(ampSource.get()))source->controls((float)namGain.getValue(),(float)namVolume.getValue());};
        ampEqGear.setComponentID("amp-eq-settings");ampEqGear.setButtonText("EQ");ampEqGear.setTitle("NAM EQ settings");ampEqGear.setTooltip("AMP EQ: shape your tone, reduce rumble and soften harsh highs");ampEqGear.setLookAndFeel(&ampLook);
        ampEqGear.onClick=[this]{if(ampEqDialog){ampEqDialog->toFront(true);return;}ampEqControls.setAdvancedOpen(false);ampEqControls.sync(outputSettings.ampEq);ampEqDialog=tonegrid::popup::launch("AMP EQ",ampEqControls,*this);};
        ampEqControls.onBodyHeight=[this](int height)
        {
            if(auto* dialog=dynamic_cast<tonegrid::popup::Window*>(ampEqDialog.getComponent()))dialog->resizeToBody(height);
        };
        ampEqControls.onChange=[this](tonegrid::ampEq::Parameters p){outputSettings.ampEq=p;outputSettings.expandedAmpEq=true;configureOutputDsp();};
        normalize.setLookAndFeel(&ampLook);normalize.setComponentID("amp-normalize");
        normalize.addItem("Normalize: OFF",1);normalize.addItem("Normalize: Partial",2);normalize.addItem("Normalize: Full",3);
        normalize.setTitle("NAM level normalization");
        normalize.onChange=[this]{outputSettings.ampNormalizationEnabled=normalize.getSelectedId()!=1;outputSettings.ampNormalizationEnhanced=normalize.getSelectedId()!=2;syncOutputUi();};
        spreadControls.sync(outputSettings);spreadControls.onChange=[this]{const auto old=outputSettings;outputSettings=spreadControls.state();outputSettings.ampNormalizationEnabled=old.ampNormalizationEnabled;outputSettings.ampNormalizationEnhanced=old.ampNormalizationEnhanced;outputSettings.reverb=old.reverb;outputSettings.pitch=old.pitch;outputSettings.mod=old.mod;outputSettings.delay=old.delay;outputSettings.expandedEffects=old.expandedEffects;outputSettings.ampEq=old.ampEq;outputSettings.expandedAmpEq=old.expandedAmpEq;syncOutputUi();};
        spreadToggle.setComponentID("3d-toggle");spreadToggle.setTitle("3D Sound ON/OFF");spreadToggle.setClickingTogglesState(true);spreadToggle.onClick=[this]{outputSettings.spreadEnabled=spreadToggle.getToggleState();syncOutputUi();};
        spreadGear.setComponentID("3d-settings");spreadGear.setTitle("3D Sound advanced settings");spreadGear.setTooltip("3D Sound settings");spreadGear.onClick=[this]{showSpreadSettings();};
        ampPanel.actions.addAndMakeVisible(normalize);ampPanel.actions.addAndMakeVisible(normalizeHelp);ampPanel.keepVisible=[this]{return normalize.isPopupActive();};normalizeHelp.setLookAndFeel(&ampLook);normalizeHelp.setTooltip("Normalization matches listening levels without changing amp drive. OFF: original level. Partial: NAM loudness metadata. Full: adds the measured ToneGrid correction. Fixed gain preserves your playing dynamics.");
        reverbToggle.setComponentID("reverb-toggle");reverbToggle.setTitle("Reverb ON/OFF");reverbToggle.setClickingTogglesState(true);reverbToggle.onClick=[this]{outputSettings.reverb.enabled=reverbToggle.getToggleState();syncOutputUi();};
        reverbGear.setComponentID("reverb-settings");reverbGear.setTitle("Reverb settings");reverbGear.onClick=[this]{showReverbSettings();};
        reverbControls.onChange=[this]{outputSettings.reverb=reverbControls.state(outputSettings.reverb.enabled);syncOutputUi();};
        pitchMode.setComponentID("pitch-mode");pitchMode.addItemList({"OFF","TRANSPOSE","OCTAVER"},1);
        pitchMode.onChange=[this]{outputSettings.pitch.mode=tonegrid::pitch::Mode(pitchMode.getSelectedId()-1);outputSettings.expandedEffects=true;syncOutputUi();};
        pitchGear.setComponentID("pitch-settings");pitchGear.setTitle("Pitch settings");pitchGear.onClick=[this]{showEffectsSettings(tonegrid::EffectsPanel::Pitch);};
        modToggle.setComponentID("mod-toggle");modToggle.setTitle("MOD ON/OFF");modToggle.setClickingTogglesState(true);modToggle.onClick=[this]{outputSettings.mod.enabled=modToggle.getToggleState();outputSettings.expandedEffects=true;syncOutputUi();};
        modGear.setComponentID("mod-settings");modGear.setTitle("MOD settings");modGear.onClick=[this]{showEffectsSettings(tonegrid::EffectsPanel::Mod);};
        delayToggle.setComponentID("delay-toggle");delayToggle.setTitle("Delay ON/OFF");delayToggle.setClickingTogglesState(true);delayToggle.onClick=[this]{outputSettings.delay.enabled=delayToggle.getToggleState();outputSettings.expandedEffects=true;syncOutputUi();};
        delayGear.setComponentID("delay-settings");delayGear.setTitle("Digital Delay settings");delayGear.onClick=[this]{showEffectsSettings(tonegrid::EffectsPanel::Delay);};
        pitchControls.onChange=[this]{outputSettings.pitch=pitchControls.pitchSettings();outputSettings.expandedEffects=true;configureOutputDsp();};
        modControls.onChange=[this]{outputSettings.mod=modControls.modSettings();outputSettings.expandedEffects=true;configureOutputDsp();};
        delayControls.onChange=[this]{outputSettings.delay=delayControls.delaySettings();outputSettings.expandedEffects=true;configureOutputDsp();};
        refreshNamLibrary();
        pluginChoice.setTextWhenNothingSelected("Choose a VST3 plugin / Scan VST3");pluginChoice.setComponentID("amp-plugin");
        pluginChoice.onChange=[this]{selectPlugin();};scanPlugins.onClick=[this]{startVst3Scan();};addPluginFolder.onClick=[this]{choosePluginFolder();};addPluginFolder.setComponentID("add-vst3-folder");scanPlugins.setComponentID("scan-vst3");
        populatePlugins();
        initialiseCabinetUi();
        title.setText("ToneGrid " JUCE_APPLICATION_VERSION_STRING,dontSendNotification);
        name.setTextToShowWhenEmpty("Rig name",Colours::grey); log.setMultiLine(true); log.setReadOnly(true);
        for (auto* c : std::initializer_list<Component*>{&ampGui,&pluginChoice,&scanPlugins,&addPluginFolder,&namSourceButton,&vstSourceButton,&ampPanel,&namGain,&namVolume,&ampEqGear,&spreadToggle,&spreadGear,&reverbToggle,&reverbGear,&modToggle,&modGear,&delayToggle,&delayGear,&pitchMode,&pitchGear,&logo,&latestLog,&audio,&slot,&save,&midiDevice,&rescan,&learn,&status,&log,&guitarIn,&gateThreshold,&gateToggle,&meter,&outputMeter,&showLog}) addAndMakeVisible(c);
        addChildComponent(namChoice);addChildComponent(refreshNam);log.setVisible(false);latestLog.setColour(Label::textColourId,tonegrid::ui::metadataInk());latestLog.setFont(tonegrid::ui::font(tonegrid::ui::statusSize));latestLog.setJustificationType(Justification::centredRight);latestLog.setInterceptsMouseClicks(false,false);
        tunerButton.setComponentID("tuner");tunerButton.setTitle("Tuner");tunerButton.setTooltip("Tune your guitar");tunerButton.setLookAndFeel(&ampLook);addAndMakeVisible(tunerButton);tunerButton.onClick=[this]{showTuner();};
        outputMeter.setComponentID("output-meter");outputMeter.setTitle("Final output level");
        outputMeter.setTooltip("Final stereo output after all effects and playback. Red: 0 dBFS or above (digital clipping). This does not measure musical amp distortion.");
        audioTap.setRecorder(&recorder);audioTap.setLooper(&looper);
        loopControls.onAdvance=[this]{if(recorder.state()!=tonegrid::recording::Mode::playing)looper.advance();};
        recordingControls.onRecord=[this]{if(auto* device=devices.getCurrentAudioDevice())recorder.record(device->getCurrentSampleRate());};
        recordingControls.onPlay=[this]{if(looper.state()==tonegrid::looping::Mode::playing||looper.state()==tonegrid::looping::Mode::recording)return;if(auto* device=devices.getCurrentAudioDevice())recorder.play(device->getCurrentSampleRate());};
        recordingControls.onExport=[this]{exportRecording();};
        versionLabel.setText(String("ToneGrid ")+JUCE_APPLICATION_VERSION_STRING,dontSendNotification);versionLabel.setColour(Label::textColourId,tonegrid::ui::secondaryInk());versionLabel.setFont(tonegrid::ui::font(tonegrid::ui::statusSize));addAndMakeVisible(versionLabel);
        checkUpdate.setComponentID("check-update");checkUpdate.setLookAndFeel(&ampLook);tonegrid::l10n::tooltip(checkUpdate,"Check for a new ToneGrid version");addAndMakeVisible(checkUpdate);
        checkUpdate.onClick=[this]{startOnlineCheck(true,false,true);};
        ampGui.onClick=[this]{if(ready()&&ampSource->type()==amp::SourceType::Vst3)showEditor();else {report("Choose a plugin from the list, or use Scan VST3 / Add folder.");if(pluginChoice.getNumItems()>0)pluginChoice.showPopup();else choosePluginFolder();}}; audio.onClick=[this]{audioSettings();};
        slot.setVisible(false);addAndMakeVisible(presetEntry);presetEntry.setComponentID("preset-browser");presetEntry.onClick=[this]{showPresetBrowser();};
        presetBrowser.onUser=[this](int index){queueUserPreset(index);};
        presetBrowser.onTone=[this](size_t index){queueTonePreset(index);};
        presetBrowser.onReload=[this]{if(appPaths.toneBankOverride().exists())reloadConfiguredToneBank();else startOnlineCheck(false,true,true);};
        presetBrowser.onMove=[this](int index,int direction){movePreset(index,direction);};
        presetBrowser.onShare=[this](int index){sharePreset(index);};
        presetBrowser.onRename=[this](int index){askRenamePreset(index);};presetBrowser.onDelete=[this](int index){askDeletePreset(index);};
        slot.onChange=[this]{toneBankActive=false;midiController.cancelLearn(); selectSlot(); if(rigStore.contains(selected()))recallSlot(selected()); else active.setText("Empty slot - current sound kept. Name it and Save Rig.",dontSendNotification);}; save.onClick=[this]{askSave();};save.setComponentID("save");
        rescan.onClick=[this]{scanMidi();}; midiDevice.onChange=[this]{openMidi();};
        learn.onClick=[this]{if(selected()<rig::slotCount)startLearn(selected());}; clear.onClick=[this]{if(selected()<rig::slotCount)clearBinding(selected());};
        if(auto xml=XmlDocument::parse(bankDir.getChildFile("midi.xml"))) if(!midiController.restoreAssignments(*xml)) report("MIDI file is invalid; assignments ignored.");
        for(auto* knob:{&guitarIn,&gateThreshold}){knob->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);knob->setTextBoxStyle(Slider::TextBoxBelow,false,65,17);knob->setTextValueSuffix(" dB");}
        guitarIn.setNumDecimalPlacesToDisplay(1);
        guitarIn.setDoubleClickReturnValue(true,0.);gateThreshold.setDoubleClickReturnValue(true,-50.);
        guitarIn.setRange(-12,12,.1);guitarIn.setValue(0,dontSendNotification);guitarIn.setTooltip("Guitar input gain before the gate and pedals (-12 to +12 dB)");
        gateThreshold.setRange(-80,-20,1);gateThreshold.setValue(-62,dontSendNotification);gateThreshold.setTooltip("Noise gate threshold in dBFS, after Guitar IN");
        gateToggle.setClickingTogglesState(true);gateToggle.setComponentID("gate");gateToggle.setTooltip("Enable input noise gate");
        guitarIn.onValueChange=gateThreshold.onValueChange=[this]{updateInput();};gateToggle.onClick=[this]{updateInput();};
        showLog.onClick=[this]{logVisible=!logVisible;log.setVisible(logVisible);showLog.setButtonText(logVisible?"Hide Log":"Show Log");resized();};
        addAndMakeVisible(sendDiagnostic);sendDiagnostic.setComponentID("send-diagnostic");sendDiagnostic.setTooltip("Prepare an email with troubleshooting information. You review and send it yourself.");sendDiagnostic.onClick=[this]{showDiagnostic();};
        learn.onClear=[this]{if(selected()<rig::slotCount)clearBinding(selected());};learn.setTooltip("Assign MIDI to this preset. Right-click to clear its assignment.");gateToggle.setTitle("Noise gate ON/OFF");
        addAndMakeVisible(playTab);addAndMakeVisible(jamTab);addAndMakeVisible(learnTab);addAndMakeVisible(recordTab);addAndMakeVisible(bassModeToggle);addAndMakeVisible(playModuleSurface);addChildComponent(jamView);addChildComponent(learnView);
        playTab.setComponentID("mode-play");jamTab.setComponentID("mode-jam");playTab.setToggleState(true,dontSendNotification);
        learnTab.setComponentID("mode-learn");playTab.onClick=[this]{setMode(0);};jamTab.onClick=[this]{setMode(1);};learnTab.onClick=[this]{setMode(2);};recordTab.onClick=[this]{setMode(3);};recordTab.setComponentID("mode-record");
        bassModeToggle.setComponentID("instrument-mode-toggle");bassModeToggle.setLookAndFeel(&ampLook);bassModeToggle.setClickingTogglesState(true);
        bassModeToggle.setTooltip("Switch between Guitar mode (13 stompboxes) and Bass mode (TRI-COMP 3-Band Distressor + TRI-DRIVE 3-Band Parallax Distortion + Bass Stompboxes).");
        bassModeToggle.onClick=[this]{setInstrumentMode(bassModeToggle.getToggleState()?tonegrid::InstrumentMode::Bass:tonegrid::InstrumentMode::Guitar);};
        initialiseRecordModule();
        jamView.configurePlayer(appPaths.user().getChildFile("JAM/WebView2"),!hardwareEnabled&&!liveValidationBrowsers);
        jamView.setCatalogueFile(appPaths.resources().getChildFile("Jam/backing-tracks.json"));jamView.setCatalogueOverride(appPaths.jamCatalogue());
        learnView.configure(appPaths.resources().getChildFile("Learn/lessons.json"),appPaths.user().getChildFile("LEARN/favourites.json"),appPaths.user().getChildFile("LEARN/WebView2"),!hardwareEnabled&&!liveValidationBrowsers);learnView.setCatalogueOverride(appPaths.learnCatalogue());
        reloadSlots(0); scanMidi(); refresh(); setSize(1660,940);
        addChildComponent(startupStatus);startupStatus.setBounds(getLocalBounds());startupStatus.setVisible(startupPresetPending);
        active.setText("Preparing your pedalboard...",dontSendNotification);
        const auto directory=appPaths.models();
        loading=std::async(std::launch::async,[this,directory,lite=namLite]{ userPedals.refresh();LoadResult r; r.lite=lite;r.processor=std::make_unique<board::Processor>(); r.result=r.processor->load(directory,lite); return r; });
        reloadConfiguredToneBank();
        save.setAlpha(0);learn.setAlpha(0);rescan.setAlpha(0);
        languageChanges=std::make_unique<tonegrid::l10n::Subscription>([this]
        {
            updateAudioSettingsStatus();
            std::function<void(Component&)> redraw=[&](Component& c){c.repaint();for(auto* child:c.getChildren())redraw(*child);};
            resized();redraw(*this);
        });
        startTimerHz(30);
    }
    ~RigHost() override
    {
        languageChanges.reset();
        presetRequests.cancel();if(presetPreparing.valid())presetPreparing.wait();
        if(startupPreparing.valid())startupPreparing.wait();
        if(record141NoDevice){record141NoDevice->shutdown();record141NoDevice.reset();}
        if(recordModule){recordModule->shutdown();audioTap.setInsert(nullptr);}
        cancelPluginValidation=true;if(pluginValidation.valid())pluginValidation.wait();
        delete missingCapturesDialog.getComponent();delete pedalMidiDialog.getComponent();
        learnView.stopPlayer();jamView.stopPlayer();
        setLookAndFeel(nullptr);
        persistSelection(); // Flush current amp EQ/session settings before releasing the graph.
        persistAudioSettings(); // Flush pending device choices even if Settings is still open.
        stopTimer();if(contentValidation.valid())contentValidation.wait();delete contentSettingsDialog.getComponent();cabinetOnlineWindow.reset();if(cabinetRefreshing.valid())cabinetRefreshing.wait();delete cabinetDialog.getComponent();delete cabinetRecommendationDialog.getComponent();if(cabinetLoading.valid())cabinetLoading.wait();delete diagnosticDialog.getComponent();delete welcomeDialog.getComponent();delete calibrationDialog.getComponent();delete calibrationWarningDialog.getComponent();onlineUpdates.stop();delete updateDialog.getComponent();tunerControls.close();delete tunerDialog.getComponent();tone3000Window.reset();pedalTone3000Window.reset();delete ampEqDialog.getComponent();delete bassRackDialog.getComponent();delete pedalImportDialog.getComponent();delete pedalDeleteDialog.getComponent();delete shareConfirmDialog.getComponent();delete shareDialog.getComponent(); if(midi) midi->stop(); midi.reset(); delete audioDialog.getComponent();delete spreadDialog.getComponent();delete reverbDialog.getComponent();delete pitchDialog.getComponent();delete modDialog.getComponent();delete delayDialog.getComponent();delete namBrowserDialog.getComponent();delete presetDialog.getComponent();
        devices.removeChangeListener(this);devices.removeAudioCallback(&audioTap); player.setProcessor(nullptr); if(ampSource)ampSource->closeEditor();
        playModule.contentView().triCompCard.reset();playModule.contentView().triDriveCard.reset();
        for(auto& c:cards)c.reset(); if(ampSource)ampSource->unload(); graph.clear(); devices.closeAudioDevice();
        if(loading.valid()) loading.wait();
        if(userLibraryScan.valid())userLibraryScan.wait();if(userLibraryOperation.valid())userLibraryOperation.wait();
        if(bankLoading.valid())bankLoading.wait();
        scanProgress.cancel.store(true);if(pluginScan.valid())pluginScan.wait();
    }
    // Isolated regression/preview workflows; no production behaviour.
#include "../../Tests/RigHostWorkflows.inl"
#include "../../Tests/Startup143Host.inl"
#include "../../Tests/Calibration117Host.inl"
#include "../../Tests/Calibration118Host.inl"
#include "../../Tests/Normalization125Host.inl"
#include "../../Tests/BetaWelcome125Host.inl"
#include "../../Tests/PrivateBeta125Host.inl"
#include "../../Tests/FactoryRefinements142Host.inl"
#include "../../Tests/Release134Host.inl"
#include "../../Tests/Release135Host.inl"
#include "../../Tests/ToneBank133Host.inl"
#include "../../Tests/AmpEq121Host.inl"
#include "../../Tests/Release122Host.inl"
#include "../../Tests/Ui123Host.inl"
#include "../../Tests/Window125Host.inl"
#include "../../Tests/AudioRecovery126Host.inl"
#include "../../Tests/NamEq123Host.inl"
#include "../../Tests/Release130Host.inl"
#include "../../Tests/Learn132Host.inl"
#include "../../Tests/Library124Host.inl"
#include "../../Tests/Record141Host.inl"
#include "App/CalibrationHost.inl"
    bool calibration117Ready() const{return ready();}
    void forceSafeStartupForTest(){startupPresetPending=true;starterBootstrapPending=false;}
    void setInstrumentMode(tonegrid::InstrumentMode mode)
    {
        const bool bass = tonegrid::isBass(mode);
        bassModeToggle.setToggleState(bass, dontSendNotification);
        bassModeToggle.setButtonText(bass ? "MODE: BASS" : "MODE: GUITAR");
        if (pedals)
            pedals->setInstrumentMode(mode);
        playModule.contentView().setInstrumentMode(mode, activeModuleIndex == 0);
        for (auto& c : cards) if (c) c->sync();
        if (playModule.contentView().triCompCard) playModule.contentView().triCompCard->sync();
        if (playModule.contentView().triDriveCard) playModule.contentView().triDriveCard->sync();
        resized();
        repaint();
    }
    tonegrid::InstrumentMode instrumentMode() const noexcept
    {
        return pedals ? pedals->instrumentMode() : (bassModeToggle.getToggleState() ? tonegrid::InstrumentMode::Bass : tonegrid::InstrumentMode::Guitar);
    }
    void showBassRackEditor(tonegrid::play::BassRackUnit unit)
    {
        if (!pedals) return;
        delete bassRackDialog.getComponent();
        auto safe = Component::SafePointer<RigHost>(this);
        auto* editor = new tonegrid::play::BassRackEditorView(
            *pedals,
            unit,
            [safe]
            {
                if (!safe) return;
                if (safe->playModule.contentView().triCompCard) safe->playModule.contentView().triCompCard->sync();
                if (safe->playModule.contentView().triDriveCard) safe->playModule.contentView().triDriveCard->sync();
            },
            [safe](int bandIdx) { if (safe) safe->chooseBandNamCapture(bandIdx); },
            [safe] { if (safe) safe->chooseTriDriveCabIr(); });
        const String title = (unit == tonegrid::play::BassRackUnit::TriComp)
                                 ? "TRI-COMP // 3-Band Distressor Compressor"
                                 : "TRI-DRIVE // 3-Band Parallax Distortion";
        bassRackDialog = tonegrid::popup::launch(title, *editor, *this, true, 520);
    }
    void chooseBandNamCapture(int bandIdx)
    {
        if (!pedals) return;
        bassRackChooser = std::make_unique<FileChooser>("Select a 48 kHz mono .nam capture for TRI-DRIVE", namLibrary.folder(), "*.nam");
        bassRackChooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
            [safe = Component::SafePointer<RigHost>(this), bandIdx](const FileChooser& fc)
            {
                if (!safe || !safe->pedals) return;
                const auto f = fc.getResult();
                if (!f.existsAsFile()) return;
                auto prepared = tonegrid::userpedals::prepare(f, safe->namLite, SHA256(f).toHexString());
                if (prepared.result.failed()) { safe->report(prepared.result.getErrorMessage()); return; }
                prepared.model.file = f.getFullPathName();
                prepared.model.label = f.getFileNameWithoutExtension();
                safe->player.setProcessor(nullptr);
                safe->pedals->triDriveProcessor().installBandNam(bandIdx, std::move(prepared.model));
                auto td = safe->pedals->getTriDrive();
                if (bandIdx == 0) td.midCharacter = tonegrid::multiband::DriveCharacter::Nam;
                else td.highCharacter = tonegrid::multiband::DriveCharacter::Nam;
                safe->pedals->setTriDrive(td);
                safe->attach();
                if (safe->playModule.contentView().triDriveCard) safe->playModule.contentView().triDriveCard->sync();
            });
    }
    void chooseTriDriveCabIr()
    {
        if (!pedals) return;
        bassRackChooser = std::make_unique<FileChooser>("Select a Cabinet IR (.wav) for TRI-DRIVE Mid+High", cabinetLibrary.root, "*.wav");
        bassRackChooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
            [safe = Component::SafePointer<RigHost>(this)](const FileChooser& fc)
            {
                if (!safe || !safe->pedals) return;
                const auto f = fc.getResult();
                if (!f.existsAsFile()) return;
                safe->player.setProcessor(nullptr);
                const auto res = safe->pedals->triDriveProcessor().loadCabIrFromFile(f);
                safe->attach();
                if (res.failed()) safe->report(res.getErrorMessage());
                if (safe->playModule.contentView().triDriveCard) safe->playModule.contentView().triDriveCard->sync();
            });
    }
    void paint(Graphics& g) override
    {
        g.setGradientFill(ColourGradient(Colour(0xff18232a),0,0,Colour(0xff04090d),(float)getWidth(),(float)getHeight(),false));g.fillAll();

        const auto metrics=tonegrid::ui::toolbarMetrics(getWidth()-2*tonegrid::ui::windowInset-tonegrid::ui::logoWidth-tonegrid::ui::logoGap);
        const StringArray headings {"AMP / SOURCE","PRESET",tonegrid::isBass(instrumentMode())?"BASS IN":"GUITAR IN","NOISE GATE","AUDIO","MIDI","PITCH"};
        for(int i=1;i<7;++i){g.setFont(tonegrid::ui::sectionFont());g.setColour(tonegrid::ui::secondaryInk());g.drawText(headings[i],i==2?Rectangle<int>(guitarIn.getX(),sections[2].getY()+4,guitarIn.getWidth(),18):sections[(size_t)i].reduced(metrics.padding,4).removeFromTop(18),i==2?Justification::centred:Justification::centredLeft);if(i!=5){g.setColour(tonegrid::ui::separator().withAlpha(tonegrid::ui::separatorOpacity));g.fillRect(sections[(size_t)i].getRight()+metrics.sectionSpacing/2,sections[(size_t)i].getY()+8,1,60);}}
        tonegrid::ampui::paintChassis(g,sections[0],displayedSource==amp::SourceType::Nam);

    }
    void resized() override
    {
        startupStatus.setBounds(getLocalBounds());
        auto b=getLocalBounds().reduced(tonegrid::ui::windowInset);b.setBottom(getHeight()-tonegrid::ui::footerInset);auto header=b.removeFromTop(tonegrid::ui::headerHeight);header.removeFromLeft(tonegrid::ui::logoWidth+tonegrid::ui::logoGap);
        const auto metrics=tonegrid::ui::toolbarMetrics(header.getWidth());
        const auto toolbar=tonegrid::ui::toolbarSections(header);
        for(int i=1;i<4;++i)sections[(size_t)i]=toolbar[(size_t)i-1];sections[6]=toolbar[3];sections[4]=toolbar[4];sections[5]=toolbar[5];logo.setBounds(tonegrid::ui::windowInset,tonegrid::ui::windowInset,tonegrid::ui::logoWidth,tonegrid::ui::headerHeight-26);
        playTab.setBounds(tonegrid::ui::windowInset+4,tonegrid::ui::windowInset+tonegrid::ui::headerHeight-26,52,24);jamTab.setBounds(playTab.getRight()+4,playTab.getY(),52,24);learnTab.setBounds(jamTab.getRight()+4,playTab.getY(),56,24);recordTab.setBounds(learnTab.getRight()+4,playTab.getY(),70,24);
        bassModeToggle.setBounds(recordTab.getRight()+12,playTab.getY(),126,24);
        b.removeFromTop(tonegrid::ui::boardTopGap);auto footer=b.removeFromBottom(logVisible?88:32);auto stat=footer.removeFromTop(30);showLog.setBounds(stat.removeFromRight(94));sendDiagnostic.setBounds(stat.removeFromRight(138).reduced(2));const int logWidth=jmax(170,jmin(stat.getWidth()/4,stat.getWidth()-600-138-170));latestLog.setBounds(stat.removeFromRight(logWidth));checkUpdate.setBounds(stat.removeFromRight(138).reduced(2));versionLabel.setBounds(stat.removeFromRight(170));status.setBounds(stat);log.setBounds(footer);
        const auto recordWorkspace=b;b.removeFromBottom(8);sections[0]=b.removeFromBottom(tonegrid::ampui::compactHeight(b.getWidth()));b.removeFromBottom(10);
        auto inner=[this,&metrics](int i){return sections[(size_t)i].reduced(metrics.padding,4).withTrimmedTop(18);};
        const auto box=[this](float x,float y,float w,float h){return tonegrid::ampui::panelBox(sections[0],x,y,w,h);};
        namSourceButton.setBounds(box(60,39,164,50));vstSourceButton.setBounds(box(60,108,164,50));
        ampPanel.setBounds(box(292,6,370,169));
        ampPanel.setRows({{&ampPanel.load},{&normalize,&normalizeHelp}});
        namChoice.setBounds(ampPanel.getBounds());
        cabinetPanel.setBounds(box(678,6,370,169));
        namGain.setBounds(tonegrid::ampui::gainBounds(sections[0]));ampEqGear.setBounds(tonegrid::ampui::eqBounds(sections[0]).withSizeKeepingCentre(50,50));namVolume.setBounds(tonegrid::ampui::volumeBounds(sections[0]));outputMeter.setBounds(tonegrid::ampui::outputMeterBounds(sections[0]));
        spreadToggle.setBounds(tonegrid::ampui::effectToggleBounds(sections[0],0));spreadGear.setBounds(tonegrid::ampui::settingsButtonBounds(sections[0],0));
        modToggle.setBounds(tonegrid::ampui::effectToggleBounds(sections[0],1));modGear.setBounds(tonegrid::ampui::settingsButtonBounds(sections[0],1));
        delayToggle.setBounds(tonegrid::ampui::effectToggleBounds(sections[0],2));delayGear.setBounds(tonegrid::ampui::settingsButtonBounds(sections[0],2));
        reverbToggle.setBounds(tonegrid::ampui::effectToggleBounds(sections[0],3));reverbGear.setBounds(tonegrid::ampui::settingsButtonBounds(sections[0],3));
        pluginChoice.setBounds(tonegrid::ampui::vstSelectorBounds(sections[0]));ampGui.setBounds(box(320,96,160,36));scanPlugins.setBounds(box(498,96,170,36));addPluginFolder.setBounds(box(686,96,170,36));
        using namespace tonegrid::ui;
        auto row=inner(1);slot.setBounds(row.removeFromLeft(metrics.preset).withHeight(buttonHeight));presetEntry.setBounds(slot.getBounds());row.removeFromLeft(metrics.controlGap);save.setBounds(row.removeFromLeft(metrics.save).withHeight(buttonHeight));row.removeFromLeft(metrics.controlGap);learn.setBounds(row.removeFromLeft(metrics.learn).withHeight(buttonHeight));
        row=inner(2);guitarIn.setBounds(row.removeFromLeft(74).withHeight(56));meter.setBounds(row.removeFromRight(14).withTrimmedBottom(2));
        row=inner(3);gateToggle.setBounds(row.removeFromLeft(metrics.gate<=130?30:42).withY(row.getY()+8).withHeight(24));row.removeFromLeft(metrics.controlGap);gateThreshold.setBounds(row.removeFromLeft(metrics.gate<=130?64:76).withHeight(56));
        row=inner(6);pitchGear.setBounds(row.removeFromRight(24).withHeight(24).withY(row.getY()+4));row.removeFromRight(4);pitchMode.setBounds(row.withHeight(buttonHeight));
        row=inner(4);tunerButton.setBounds(row.removeFromLeft(buttonHeight).withHeight(buttonHeight));row.removeFromLeft(metrics.controlGap);audio.setBounds(row.removeFromLeft(metrics.audio).withHeight(buttonHeight));row.removeFromLeft(metrics.controlGap);guitarCalibration.setBounds(row.removeFromLeft(metrics.calibration).withHeight(buttonHeight));
        row=inner(5);midiDevice.setBounds(row.removeFromLeft(metrics.midi).withHeight(buttonHeight));row.removeFromLeft(metrics.midiGap);rescan.setBounds(row.removeFromLeft(metrics.refresh).withHeight(buttonHeight));rescan.setButtonText(metrics.refresh<=32?String():"Refresh");
        pedalArea=b;playModuleSurface.setBounds(b);jamView.setBounds(b);learnView.setBounds(b);if(recordModule)recordModule->view().setBounds(recordWorkspace);playModule.contentView().resized();

    }

private:
    void setJam(bool active)
    {setMode(active?1:0);}
    void setMode(int mode)
    {
        if(mode<0||mode>3||mode==activeModuleIndex)return;
        if(modules[size_t(activeModuleIndex)])modules[size_t(activeModuleIndex)]->deactivated();
        activeModuleIndex=mode;jamActive=mode==1;learnActive=mode==2;recordActive=mode==3;
        if(recordActive){recorder.stop();}
        playTab.setToggleState(mode==0,dontSendNotification);jamTab.setToggleState(jamActive,dontSendNotification);learnTab.setToggleState(learnActive,dontSendNotification);recordTab.setToggleState(recordActive,dontSendNotification);
        playModule.contentView().setInstrumentMode(instrumentMode(), mode==0);
        recordingControls.setVisible(mode==0);loopControls.setVisible(mode==0);
        for(size_t n=0;n<modules.size();++n)if(modules[n])modules[n]->view().setVisible(int(n)==mode);
        if(modules[size_t(mode)]){modules[size_t(mode)]->activated();if(mode==3)modules[size_t(mode)]->view().toFront(false);}resized();repaint();
    }
    const tonegrid::paths::Layout appPaths;
    PedalCard::CardLook look;tonegrid::ampui::Look ampLook;
    struct LoadResult { std::unique_ptr<board::Processor> processor; Result result=Result::ok(); bool lite=false; };
    std::future<LoadResult> loading;
    std::unique_ptr<board::Processor> pendingBoard;
    board::Processor* pedals=nullptr;
    // Keep the source's VST3 format manager alive until after graph destruction.
    std::unique_ptr<amp::AmpSource> ampSource;
    amp::Retirement retiredAmps; // Before graph: managers outlive old render nodes.
    AudioDeviceManager devices; rig::StereoGraph graph;tonegrid::audio::DeviceRateProcessor deviceProcessor{graph};std::atomic<bool> audioSettingsDirty{true}; AudioProcessorPlayer player;
    tonegrid::recording::SessionRecorder recorder{appPaths.encoder()};
    tonegrid::looping::Engine looper;
    tonegrid::modules::PlayModule playModule{recorder,looper};
    tonegrid::looping::Controls& loopControls=playModule.contentView().loopControls;
    tonegrid::recording::Controls& recordingControls=playModule.contentView().recordingControls;std::unique_ptr<FileChooser> recordingChooser;
    tonegrid::tuning::Engine tunerEngine;tonegrid::tuning::AudioTap audioTap{player,tunerEngine};
    tonegrid::tuning::Controls tunerControls{tunerEngine,audioTap};tonegrid::l10n::TextButton tunerButton;Component::SafePointer<DialogWindow> tunerDialog;
    AudioProcessorGraph::Node::Ptr ampStage;
    AudioProcessorGraph::NodeID pedalStageId,outputStageId;
    AudioProcessorGraph::NodeID normalizationStageId,cabinetStageId;
    tonegrid::cabinet::Node* cabinetNode=nullptr;
    tonegrid::output::AmpEqNode* ampEqNode=nullptr;
    tonegrid::output::NormalizationNode* normalizationNode=nullptr;tonegrid::output::SpreadNode* spreadNode=nullptr;tonegrid::output::ReverbNode* reverbNode=nullptr;
    tonegrid::output::Settings outputSettings=tonegrid::output::freshSessionSettings();
    tonegrid::output::ModNode* modNode=nullptr;tonegrid::output::DelayNode* delayNode=nullptr;
    tonegrid::Logo logo;tonegrid::jam::Tab playTab{"PLAY"},jamTab{"JAM"},learnTab{"LEARN"},recordTab{"RECORD"};tonegrid::l10n::TextButton bassModeToggle{"MODE: GUITAR"};tonegrid::modules::JamModule jamModule;tonegrid::modules::LearnModule learnModule;
    tonegrid::jam::View& jamView=jamModule.contentView();tonegrid::learn::View& learnView=learnModule.contentView();bool jamActive=false,learnActive=false,recordActive=false;
    Component& playModuleSurface=playModule.view();
    std::unique_ptr<tonegrid::record::RecordModule> recordModule;
    std::array<tonegrid::core::IToneGridModule*,4> modules{};int activeModuleIndex=0;ComboBox pitchMode;tonegrid::l10n::TextButton pitchGear,modToggle,modGear,delayToggle,delayGear;
    tonegrid::EffectsControls pitchControls{tonegrid::EffectsPanel::Pitch},modControls{tonegrid::EffectsPanel::Mod},delayControls{tonegrid::EffectsPanel::Delay};
    Component::SafePointer<DialogWindow> pitchDialog,modDialog,delayDialog;
    tonegrid::SpreadControls spreadControls;ComboBox normalize;tonegrid::l10n::TextButton normalizeHelp{"i"};tonegrid::l10n::TextButton spreadToggle,spreadGear;
    tonegrid::ReverbControls reverbControls;tonegrid::l10n::TextButton reverbToggle,reverbGear;
    vst3::Catalog pluginCatalog;std::optional<PluginDescription> startupPlugin;
    std::optional<namamp::State> startupNam;namamp::Catalog namCatalog; amp::SourceType displayedSource=amp::SourceType::Vst3;
    vst3::ScanProgress scanProgress;std::future<vst3::ScanResult> pluginScan;
    std::array<std::unique_ptr<PedalCard>,board::count>& cards=playModule.contentView().cards;
    Component::SafePointer<DialogWindow> audioDialog,spreadDialog,reverbDialog,namBrowserDialog;
    tonegrid::NamLibraryBrowser namBrowser;
    #include "App/CabinetHost.inl"
#include "App/PopupOnboardingHost.inl"
#include "App/Optimized140Host.inl"
    std::unique_ptr<tonegrid::t3k::Tone3000BrowserWindow> tone3000Window;
    bool tone3000OfflineTest=false;String tone3000TestAuthorization,pendingImportedNam;
    std::unique_ptr<FileChooser> ampChooser;
    Rectangle<int> pedalArea;std::array<Rectangle<int>,7> sections;
    std::unique_ptr<tonegrid::l10n::Subscription> languageChanges;
    tonegrid::motion::HoverValueSlider guitarIn,gateThreshold;double lastMotionTime=0;float recordingHintOpacity=0,presetActionOpacity=0,midiRefreshOpacity=0;tonegrid::l10n::TextButton gateToggle{"OFF"},showLog{"Show Log"},sendDiagnostic{"Send Diagnostic"};tonegrid::Meter meter,outputMeter;bool logVisible=false;
    Component::SafePointer<DialogWindow> diagnosticDialog;
    std::function<bool(const String&)> diagnosticEmailLauncher=[](const String& uri){return URL(uri).launchInDefaultBrowser();};
    tonegrid::CompactLogLabel latestLog;Label captureHeading,namFilename,namFolderName;tonegrid::ampui::Thumbnail thumbnail;
    tonegrid::l10n::TextButton namSourceButton{"NAM"},vstSourceButton{"VST"};tonegrid::libraryui::GearCard ampPanel{"Amp"};tonegrid::libraryui::ArtworkButton& namFolder=ampPanel.browse;
    tonegrid::ampui::AdvancedEqControls ampEqControls;tonegrid::ampui::EqButton ampEqGear;
    Component::SafePointer<DialogWindow> ampEqDialog,bassRackDialog,pedalImportDialog,shareConfirmDialog,shareDialog;
    std::unique_ptr<FileChooser> bassRackChooser;
    const tonegrid::userpedals::Library userPedals{appPaths.user().getChildFile("NAM/Pedals")};
    std::future<bool> userLibraryScan;std::future<Result> userLibraryOperation;int userLibraryTicks=0;String librarySuccess;
    Component::SafePointer<DialogWindow> pedalDeleteDialog;
    std::unique_ptr<tonegrid::t3k::Tone3000BrowserWindow> pedalTone3000Window;
    std::unique_ptr<FileChooser> pedalChooser;std::future<tonegrid::userpedals::Prepared> userPedalLoading;
    int pendingUserPedal=-1;String pendingUserLabel,pendingUserUrl;bool pendingUserImport=false;
    #include "App/UserPedalHost.inl"
    #include "App/PresetLoadingHost.inl"
    #include "App/PresetShareHost.inl"
    ComboBox namChoice;Slider namGain,namVolume;tonegrid::l10n::TextButton refreshNam{"Refresh NAM"};
    std::unique_ptr<MidiInput> midi; Array<MidiDeviceInfo> midiDevices;
    board::MidiController midiController;
    Component::SafePointer<tonegrid::PedalMidiLearnView> pedalMidiView;
    Component::SafePointer<DialogWindow> pedalMidiDialog;
    int pedalMidiTarget=-1;
    bool audioReady=false, audioFault=false,audioCallbackAttached=false,audioInitialisationAttempted=false; String lastAudioState,lastAudioError; int statusTicks=0;
    const bool hardwareEnabled,liveValidationBrowsers;
    const bool bundledBootstrapTest;bool starterBootstrapPending=false,startupPresetPending=false;
    Label title,active,versionLabel;tonegrid::ui::StatusLabel status; TextEditor path,name,log; ComboBox slot,midiDevice,pluginChoice;
    tonegrid::l10n::TextButton scanPlugins{"Scan VST3"},addPluginFolder{"Add folder..."};
    std::unique_ptr<FileChooser> pluginFolderChooser;std::future<vst3::ValidationResult> pluginValidation;std::atomic<bool> cancelPluginValidation{false};bool validatedRecall=false;unsigned pluginLoadGeneration=0,activeValidationGeneration=0;
    String startupProblem;bool startupProblemShown=false,blockAutomaticVst=false;
    tonegrid::l10n::TextButton ampGui{"Open Amp"},audio{"Settings"},save{"Save"},rescan{"Refresh MIDI"},clear{"Clear rig MIDI"};tonegrid::LearnButton learn{"MIDI Learn"};
    tonegrid::SoftTooltip tooltips{this,520};
    const board::RigStore rigStore;
    const File bankDir=rigStore.directory(); // Non-rig preferences/MIDI/logging stay in this host.
    const File toneBankCache=appPaths.toneBankCache();
    tonegrid::tonebank::Bank toneBank;
    String currentPresetId=Uuid().toString(),currentPresetExtensions;
    std::future<tonegrid::tonebank::CacheResult> bankLoading;
#include "App/EditableContentHost.inl"
    tonegrid::updates::Service onlineUpdates{appPaths.onlineChecks(),toneBankCache,TONEGRID_VERSION_NUMERIC};
    Component::SafePointer<tonegrid::MissingCapturesView> missingCaptureView;
    bool missingApplicationCheck=false;
    tonegrid::l10n::TextButton checkUpdate{"Check for update"};Component::SafePointer<DialogWindow> updateDialog;
    bool manualApplicationCheck=false;
    double nextOnlineSchedule=Time::getMillisecondCounterHiRes()+15000;
    tonegrid::PresetBrowser presetBrowser;
    TextButton presetEntry{"Choose preset"}; // Displays verbatim user/song names.
    Component::SafePointer<DialogWindow> presetDialog;
    Component::SafePointer<DialogWindow> missingCapturesDialog;
    bool toneBankActive=false;
    const File vst3Directory=appPaths.vst3();
    const namamp::Library namLibrary{appPaths.namAmps()};
    namamp::LibraryService namService{namLibrary,(hardwareEnabled||bundledBootstrapTest)?appPaths.factoryAmps():File{}};bool namTreeDirty=true;tonegrid::ThumbnailCache thumbnailCache;
    const File calibrationPreference=appPaths.levelCalibration();tonegrid::calibration::Settings calibrationSettings;
    tonegrid::calibration::Action guitarCalibration;
    tonegrid::popup::Backdrop modalBackdrop{*this};
    Component::SafePointer<DialogWindow> calibrationDialog,calibrationWarningDialog,welcomeDialog;
    bool welcomeOffered=false;
    tonegrid::calibration::WarningStore calibrationWarnings{calibrationPreference.getSiblingFile("capture-warnings.xml")};
    std::optional<namamp::Entry> pendingCalibrationWarning;String lastCalibrationWarningKey;
    bool calibrationOnboardingOffered=false;
    const File qualityPreference=appPaths.namQuality();bool namLite=false;
    const File sourcePreference=appPaths.ampSelection();
    const vst3::CacheStore cacheStore{vst3Directory.getChildFile("plugins.xml")},selectionStore{vst3Directory.getChildFile("selected.xml")};
    board::RigIdentityLookup rigIdentities() const
    { return [this](int p,int preset){const auto identity=pedals->identity(p,preset);if(board::isNam(p)&&identity.isNotEmpty()&&!(preset==board::presetCount(p)?userPedals.file(p,identity):appPaths.models().getChildFile(pedals->modelFile(p,preset))).existsAsFile())return String{};return identity;}; }
    void updateInput(){if(pedals)pedals->front.set({(float)guitarIn.getValue(),(float)gateThreshold.getValue(),gateToggle.getToggleState()});gateToggle.setButtonText(gateToggle.getToggleState()?"ON":"OFF");}
    void syncInput(){if(!pedals)return;auto s=pedals->front.get();guitarIn.setValue(s.gainDb,dontSendNotification);gateThreshold.setValue(s.thresholdDb,dontSendNotification);gateToggle.setToggleState(s.gate,dontSendNotification);gateToggle.setButtonText(s.gate?"ON":"OFF");}
    void chooseAmp()
    {
        ampChooser=std::make_unique<FileChooser>("Locate TONEX.vst3",File(path.getText()),"*.vst3");
        ampChooser->launchAsync(FileBrowserComponent::openMode|FileBrowserComponent::canSelectFiles|FileBrowserComponent::canSelectDirectories,[safe=Component::SafePointer<RigHost>(this)](const FileChooser& chooser){if(safe){auto f=chooser.getResult();if(f.exists()){safe->startupPlugin.reset();safe->path.setText(f.getFullPathName(),false);safe->loadRig();}}});
    }
    void populatePlugins()
    {
        if(ampSource&&ampSource->loaded()&&ampSource->pluginDescription())pluginCatalog.add(*ampSource->pluginDescription());
        pluginCatalog.sort();pluginChoice.clear(dontSendNotification);int selectedId=0;
        for(size_t i=0;i<pluginCatalog.plugins.size();++i)
        {
            const auto& d=pluginCatalog.plugins[i];auto text=vst3::label(d);int duplicates=0;
            for(const auto& other:pluginCatalog.plugins)if(vst3::label(other)==text)++duplicates;
            if(duplicates>1)text+=" ["+String::toHexString(d.uniqueId)+" / "+File(d.fileOrIdentifier).getFileName()+"]";
            pluginChoice.addItem(text,(int)i+1);
            if(ampSource&&ampSource->loaded()&&ampSource->pluginDescription()&&vst3::Identity::of(d)==vst3::Identity::of(*ampSource->pluginDescription())&&d.fileOrIdentifier==ampSource->pluginDescription()->fileOrIdentifier)selectedId=(int)i+1;
        }
        pluginChoice.setSelectedId(selectedId,dontSendNotification);
        pluginChoice.setTooltip(ampSource&&ampSource->loaded()&&ampSource->pluginDescription()?vst3::label(*ampSource->pluginDescription())+"\n"+ampSource->pluginDescription()->fileOrIdentifier:"Choose a cached VST3 or click Scan VST3.");
        scanPlugins.setButtonText(cacheStore.file().existsAsFile()?"Rescan VST3":"Scan VST3");
    }
    String ampName() const
    {
        if(!ampSource)return "No amp";if(const auto* d=ampSource->pluginDescription())return d->name;
        if(const auto* source=dynamic_cast<const amp::NamAmp*>(ampSource.get()))return source->state().model.file;return "No amp";
    }
    void populateNam()
    {
        namChoice.clear(dontSendNotification);int selection=0;const auto* current=dynamic_cast<const amp::NamAmp*>(ampSource.get());
        for(size_t index=0;index<namCatalog.entries.size();++index)
        {const auto& entry=namCatalog.entries[index];namChoice.addItem(entry.identity.file,(int)index+1);if(current&&current->state().model==entry.identity)selection=(int)index+1;}
        namChoice.setSelectedId(selection,dontSendNotification);namChoice.setTooltip("Local NAM heads and complete rigs.\n"+namLibrary.folder().getFullPathName());
        const auto relative=current?current->state().model.file:String{};
        if(namTreeDirty){namBrowser.update(namCatalog,namLibrary.folder(),relative);namTreeDirty=false;}else namBrowser.selectActive(relative);
        namFilename.setText(relative.isEmpty()?"Choose a local .nam capture":namamp::filename(relative),dontSendNotification);namFilename.setTooltip(relative);
        namFolderName.setText(relative.isEmpty()?"Local NAM Library":namamp::folderLabel(relative),dontSendNotification);namFolderName.setTooltip(relative);
        const auto* cached=current?&thumbnailCache.get(namLibrary.folder().getChildFile(relative)):nullptr;
        Image ampImage;
        if(current&&namCatalog.browser){auto it=namCatalog.browser->captures.find(relative);if(it!=namCatalog.browser->captures.end())ampImage=it->second.artwork;}
        if(!ampImage.isValid()&&cached)ampImage=cached->image;
        if(!ampImage.isValid()){thumbnail.setSize(300,110);thumbnail.setImage({});ampImage=thumbnail.createComponentSnapshot(thumbnail.getLocalBounds());}
        ampPanel.setPresentation(ampImage,namFilename.getText());const auto metadata=cached?cached->metadata:var{};
        if(metadata.isObject())namFilename.setTooltip(relative+"\nTONE3000: "+tonegrid::t3k::textField(metadata,"title")+"\nCreator: "+tonegrid::t3k::textField(metadata,"creator")+"\nLicense: "+tonegrid::t3k::textField(metadata,"license"));
    }
    void refreshNamLibrary()
    {
        namService.refresh();namBrowser.setRefreshing(true);
    }
    void startOnlineCheck(bool application,bool bank,bool manual)
    {
        if((!application&&!bank)||bankLoading.valid())return;
        if(onlineUpdates.start(application,bank,manual))
        {manualApplicationCheck=manual&&application;checkUpdate.setEnabled(false);checkUpdate.setButtonText("Checking...");presetBrowser.setReloading(true);}
    }
    void pollOnlineUpdates()
    {
        if(missingApplicationCheck&&missingCaptureView&&!onlineUpdates.busy()&&!bankLoading.valid())startOnlineCheck(true,false,true);
        checkUpdate.setEnabled(!onlineUpdates.busy()&&!bankLoading.valid());
        if(hardwareEnabled&&!bankLoading.valid()&&Time::getMillisecondCounterHiRes()>=nextOnlineSchedule)
        {nextOnlineSchedule=Time::getMillisecondCounterHiRes()+60000;startOnlineCheck(true,true,false);}
        tonegrid::updates::Result result;
        if(!onlineUpdates.poll(result))return;
        checkUpdate.setEnabled(true);checkUpdate.setButtonText("Check for update");presetBrowser.setReloading(false);
        if(result.checkedBank)
        {
            if(result.bankStatus.failed())report(result.bankStatus.getErrorMessage());
            else if(result.bankChanged&&!appPaths.toneBankOverride().exists()){toneBank=std::move(result.bank);presetBrowser.setBank(toneBank,"ToneBank updated.");report("ToneBank updated. Current sound kept.");}
        }
        if(result.scheduleStatus.failed())report(result.scheduleStatus.getErrorMessage());
        if(result.checkedApplication&&missingApplicationCheck)
        {
            if(missingCaptureView)missingCaptureView->setUpdateResult(result.newVersion,result.latestVersion,result.applicationStatus.failed());
            missingApplicationCheck=false;
        }
        else if(result.checkedApplication&&(manualApplicationCheck||result.newVersion))
        {
            const auto message=result.applicationStatus.failed()?result.applicationStatus.getErrorMessage():result.newVersion?tonegrid::l10n::format("update.available",{{"version",result.latestVersion}}):tonegrid::l10n::tr("update.current");
            if(auto* previous=updateDialog.getComponent())delete previous;
            auto* prompt=new tonegrid::popup::Prompt(message,result.newVersion?"Click here":"OK");
            updateDialog=tonegrid::popup::launch("ToneGrid update",*prompt,*this,true,0,ModalCallbackFunction::create(
                [available=result.newVersion](int answer){if(answer==1&&available)URL(tonegrid::updates::downloadUrl).launchInDefaultBrowser();}));
        }
        manualApplicationCheck=false;
    }
    void pollNamLibrary()
    {
        namamp::Catalog catalog;Result result=Result::ok();
        if(namService.poll(catalog,result))
        {
            if(result.wasOk()){namCatalog=std::move(catalog);namTreeDirty=true;populateNam();syncCabinetMode();if(!namCatalog.failures.empty())report(String(int(namCatalog.failures.size()))+" incompatible NAM files cannot be loaded. First: "+namCatalog.failures.front().file+": "+namCatalog.failures.front().reason);}
            else report(result.getErrorMessage());
        }
        namBrowser.setRefreshing(namService.busy());
    }
    // Test-only synchronization, with no nested message loop or hardware IO.
    void awaitNamRefreshForTest()
    {
        if(hardwareEnabled)return;
        const auto deadline=Time::getMillisecondCounter()+30000;
        while(namService.busy()&&Time::getMillisecondCounter()<deadline){pollNamLibrary();pollCabinetLoad();Thread::sleep(2);}
    }
    void syncAmpUi(bool keepPanel=false)
    {
        if(!keepPanel&&ready())displayedSource=ampSource->type();namSourceButton.setToggleState(displayedSource==amp::SourceType::Nam,dontSendNotification);vstSourceButton.setToggleState(displayedSource==amp::SourceType::Vst3,dontSendNotification);
        if(const auto* source=dynamic_cast<const amp::NamAmp*>(ampSource.get())){const auto state=source->state();namGain.setValue(state.gainDb,dontSendNotification);namVolume.setValue(state.volumeDb,dontSendNotification);}
        syncOutputUi();populateNam();populatePlugins();syncCabinetMode();resized();refresh();updateCalibrationStatus();repaint();
    }
    void configureOutputDsp()
    {
        syncCabinetMode();
        if(auto* source=dynamic_cast<amp::NamAmp*>(ampSource.get())){source->setCalibration(calibrationSettings);source->setUpstreamReference(pedals?&pedals->referenceRouting:nullptr);}
        const auto* native=dynamic_cast<const amp::NamAmp*>(ampSource.get());
        if(normalizationNode)normalizationNode->configure(outputSettings.ampNormalizationEnabled,native!=nullptr,native?native->loudnessDb():std::nullopt,native?tonegrid::output::selectedNormalizationTrim(outputSettings,native->normalizationTrimDb()):0.f);
        if(ampEqNode)ampEqNode->configure(outputSettings.ampEq,native!=nullptr);
        if(spreadNode)spreadNode->configure(outputSettings);
        if(reverbNode)reverbNode->configure(outputSettings.reverb);
        if(pedals)pedals->pitch.configure(outputSettings.pitch);
        if(modNode)modNode->configure(outputSettings.mod);if(delayNode)delayNode->configure(outputSettings.delay);
    }
    void syncOutputUi()
    {
        ampEqControls.sync(outputSettings.ampEq);configureOutputDsp();normalize.setSelectedId(!outputSettings.ampNormalizationEnabled?1:outputSettings.ampNormalizationEnhanced?3:2,dontSendNotification);spreadControls.sync(outputSettings);spreadToggle.setToggleState(outputSettings.spreadEnabled,dontSendNotification);
        reverbToggle.setToggleState(outputSettings.reverb.enabled,dontSendNotification);reverbControls.sync(outputSettings.reverb);
        pitchMode.setSelectedId(int(outputSettings.pitch.mode)+1,dontSendNotification);pitchControls.sync(outputSettings.pitch);
        modToggle.setToggleState(outputSettings.mod.enabled,dontSendNotification);modControls.sync(outputSettings.mod);
        delayToggle.setToggleState(outputSettings.delay.enabled,dontSendNotification);delayControls.sync(outputSettings.delay);
        normalize.setTooltip(String("OFF: no level correction.\nPartial: original normalization using the NAM loudness metadata.\nFull: Partial plus ToneGrid measured perceived-volume correction, when available.\nCorrections are fixed gains after the amp; your playing dynamics remain intact.")
            +(dynamic_cast<amp::NamAmp*>(ampSource.get())&&!dynamic_cast<amp::NamAmp*>(ampSource.get())->hasNormalizationTrim()?"\nThis capture has no ToneGrid correction: Full currently behaves like Partial.":""));
    }
    void showSpreadSettings()
    {
        if(spreadDialog){spreadDialog->toFront(true);return;}
        spreadControls.sync(outputSettings);spreadControls.setSize(700,324);
        spreadDialog=tonegrid::popup::launch("3D Sound",spreadControls,*this);
    }
    void showReverbSettings()
    {
        if(reverbDialog){reverbDialog->toFront(true);return;}
        reverbControls.sync(outputSettings.reverb);reverbControls.setSize(700,404);
        reverbDialog=tonegrid::popup::launch("Reverb",reverbControls,*this);
    }
    void showEffectsSettings(tonegrid::EffectsPanel panel)
    {
        auto* controls=panel==tonegrid::EffectsPanel::Pitch?&pitchControls:panel==tonegrid::EffectsPanel::Mod?&modControls:&delayControls;
        auto* dialog=panel==tonegrid::EffectsPanel::Pitch?&pitchDialog:panel==tonegrid::EffectsPanel::Mod?&modDialog:&delayDialog;
        if(*dialog){(*dialog)->toFront(true);return;}
        syncOutputUi();controls->setSize(700,324);
        *dialog=tonegrid::popup::launch(panel==tonegrid::EffectsPanel::Pitch?"Pitch":panel==tonegrid::EffectsPanel::Mod?"Modulation":"Digital Delay",*controls,*this);
    }
    void showNamChooser()
    {
        if(namBrowserDialog){namBrowserDialog->toFront(true);return;}
        populateNam();if(const auto* current=dynamic_cast<const amp::NamAmp*>(ampSource.get()))namBrowser.reveal(current->state().model.file);else namBrowser.reveal({});namBrowser.setSize(1240,640);namBrowserDialog=tonegrid::popup::launch("Local NAM Library",namBrowser,*this,false,440);
        if(pendingImportedNam.isNotEmpty()){namBrowser.reveal(pendingImportedNam);pendingImportedNam.clear();}
    }
    void tone3000ImportFinished(const tonegrid::t3k::ImportResult& result)
    {
        auto message=result.message;
        if(result.ok)
        {
            namService.merge(result.entries,namCatalog);namTreeDirty=true;populateNam();
            if(result.entries.empty())refreshNamLibrary();
            if(result.folder.isAChildOf(namLibrary.folder()))
            {
                const auto folder=result.folder.getRelativePathFrom(namLibrary.folder()).replaceCharacter('\\','/');
                const auto relative=namBrowser.firstModelIn(folder);
                for(size_t i=0;i<namCatalog.entries.size();++i)if(namCatalog.entries[i].identity.file==relative)
                {
                    namChoice.setSelectedId((int)i+1,dontSendNotification);selectNam();
                    const auto* loaded=dynamic_cast<amp::NamAmp*>(ampSource.get());
                    if(loaded&&loaded->state().model.file==relative)
                    {pendingImportedNam=relative;message=message.replace("Select a model in Local NAM Library.","Loaded "+namamp::filename(relative)+".");}
                    else if(pendingHead&&pendingHead->model.file==relative){pendingImportedNam=relative;message+=" Cabinet selection pending.";}else message+=" Automatic NAM loading failed; previous amp kept.";
                    break;
                }
            }
        }
        namBrowser.setImportStatus(message);report(message);
    }
    void commitSource(std::unique_ptr<amp::AmpSource> candidate,bool newNamState=false)
    {
        const auto previousNormalization=outputSettings.ampNormalizationEnabled;const auto previousEnhanced=outputSettings.ampNormalizationEnhanced;if(newNamState){outputSettings.ampNormalizationEnabled=true;outputSettings.ampNormalizationEnhanced=true;}
        if(!ready())
        {
            if(const auto* source=dynamic_cast<const amp::NamAmp*>(candidate.get())){startupNam=source->state();startupPlugin.reset();}
            else {startupNam.reset();startupPlugin=*candidate->pluginDescription();}
            loadRig(std::move(candidate));if(ready())persistSelection();else {outputSettings.ampNormalizationEnabled=previousNormalization;outputSettings.ampNormalizationEnhanced=previousEnhanced;}return;
        }
        const bool reopen=ampSource->editorVisible();player.setProcessor(nullptr);
        const auto result=amp::replace(graph,ampSource,ampStage,pedalStageId,cabinetStageId,std::move(candidate),retiredAmps);
        if(result.failed()){outputSettings.ampNormalizationEnabled=previousNormalization;outputSettings.ampNormalizationEnhanced=previousEnhanced;}configureOutputDsp();attach();if(reopen&&ampSource->type()==amp::SourceType::Vst3)showEditor();
        if(result.failed()){report(result.getErrorMessage());syncAmpUi();return;}
        displayedSource=ampSource->type();syncAmpUi();persistSelection();report("Active amp: "+ampName());
    }
    void selectNam()
    {
        presetRequests.cancel();
        const auto index=namChoice.getSelectedId()-1;if(!pedals||index<0||index>=(int)namCatalog.entries.size())return;
        ++cabinetSelectionGeneration;pendingHead.reset();
        namamp::State wanted{namCatalog.entries[(size_t)index].identity,0,0};
        if(const auto* source=dynamic_cast<const amp::NamAmp*>(ampSource.get()))
        {const auto state=source->state();if(state.model==wanted.model)return;wanted.gainDb=state.gainDb;wanted.volumeDb=state.volumeDb;}
        if(proposeRecommendedCabinet(wanted))return;
        auto loaded=amp::loadNamAmp(namLibrary,wanted,namLite);if(loaded.result.failed()){report(loaded.result.getErrorMessage()+" Previous amp kept.");syncAmpUi();return;}commitSource(std::move(loaded.source),dynamic_cast<amp::NamAmp*>(ampSource.get())==nullptr);queueCaptureWarning(namCatalog.entries[(size_t)index]);
    }
    void changeSource(amp::SourceType requested)
    {
        ++cabinetSelectionGeneration;pendingHead.reset();++pluginLoadGeneration;if(pluginValidation.valid())cancelPluginValidation=true;
        displayedSource=requested;syncAmpUi(true);
        if(requested==amp::SourceType::Vst3)
        {report("Choose a VST3 plugin, scan installed plugins or add a custom folder. Current sound is kept until a plugin loads.");return;}
        if(!pedals)return;if(ready()&&ampSource->type()==amp::SourceType::Nam)return;
        if(namCatalog.entries.empty()){report("No NAM captures found. Open the local library to import one. Current sound kept.");return;}
        namChoice.setSelectedId(1,dontSendNotification);selectNam();
    }
    void choosePluginFolder()
    {
        pluginFolderChooser=std::make_unique<FileChooser>("Choose your VST3 plugin folder",File::getSpecialLocation(File::globalApplicationsDirectory),"*");
        pluginFolderChooser->launchAsync(FileBrowserComponent::openMode|FileBrowserComponent::canSelectDirectories,[safe=Component::SafePointer<RigHost>(this)](const FileChooser& chooser)
        {
            if(!safe)return;const auto chosen=chooser.getResult();if(!chosen.isDirectory())return;
            auto paths=FileSearchPath(safe->vst3Directory.getChildFile("scan-paths.txt").loadFileAsString());paths.add(chosen);paths.removeRedundantPaths();
            if(safe->vst3Directory.createDirectory().failed()||!safe->vst3Directory.getChildFile("scan-paths.txt").replaceWithText(paths.toString())){safe->report("Cannot save the custom plugin folder. Check your user-folder permissions.");return;}
            safe->startVst3Scan();
        });
    }
    void requestPluginValidation(const PluginDescription& plugin,int recall=-1,const MemoryBlock& state={})
    {
        if(pluginValidation.valid())return;cancelPluginValidation=false;
        activeValidationGeneration=pluginLoadGeneration;
        pluginValidation=std::async(std::launch::async,[this,plugin,recall,state,exe=File::getSpecialLocation(File::currentExecutableFile)]{return vst3::validate(exe,plugin,state,recall,cancelPluginValidation);});
        report("Checking "+plugin.name+" in a separate process. Current sound kept.");scanPlugins.setButtonText("Checking plugin...");refresh();
    }
    amp::LoadResult loadValidatedPlugin(const PluginDescription& plugin,const MemoryBlock* state=nullptr)
    {
        const auto marker=vst3Directory.getChildFile("loading.xml");
        if(!rig::writeXml(*plugin.createXml(),marker))return {nullptr,Result::fail("Cannot save plugin recovery information. Check your user-folder permissions."),{}};
        auto loaded=vst3::load(plugin,state);marker.deleteFile();return loaded;
    }
    void persistSelection()
    {
        if(!ready())return;XmlElement preference("AmpSelection");preference.setAttribute("version",1);
        if(auto* source=dynamic_cast<amp::NamAmp*>(ampSource.get())){preference.setAttribute("type","NAM");preference.addChildElement(new XmlElement(namamp::encode(source->state())));}
        else {preference.setAttribute("type","VST3");vst3::Catalog chosen;chosen.add(*ampSource->pluginDescription());if(!selectionStore.write(chosen))report("Amp is active but its startup selection could not be saved.");}
        preference.addChildElement(new XmlElement(tonegrid::output::encode(outputSettings)));
        if(!rig::writeXml(preference,sourcePreference))report("Amp source is active but its startup mode could not be saved.");
    }
    void startVst3Scan()
    {
        if(pluginScan.valid())return;scanProgress.cancel.store(false);scanProgress.completed.store(0);scanProgress.total.store(0);
        const auto executable=File::getSpecialLocation(File::currentExecutableFile);
        auto locations=vst3::defaultScanLocations();locations.addPath(FileSearchPath(vst3Directory.getChildFile("scan-paths.txt").loadFileAsString()));locations.removeRedundantPaths();
        pluginScan=std::async(std::launch::async,[this,executable,locations]{return vst3::scan(executable,locations,scanProgress);});
        scanPlugins.setButtonText("Scanning...");report("Manual VST3 scan started; active amp kept.");refresh();
    }
    void selectPlugin(bool validated=false)
    {
        presetRequests.cancel();
        const int index=pluginChoice.getSelectedId()-1;
        if(index<0||index>=(int)pluginCatalog.plugins.size()||!pedals)return;
        const auto wanted=pluginCatalog.plugins[(size_t)index];
        if(ready()&&ampSource->pluginDescription()&&vst3::Identity::of(wanted)==vst3::Identity::of(*ampSource->pluginDescription())&&wanted.fileOrIdentifier==ampSource->pluginDescription()->fileOrIdentifier)return;
        PluginDescription resolved;const auto resolution=pluginCatalog.resolve(wanted,resolved);
        if(resolution.failed()){report(resolution.getErrorMessage());populatePlugins();return;}
        if(!validated){++pluginLoadGeneration;requestPluginValidation(resolved);return;}
        auto loaded=loadValidatedPlugin(resolved);
        if(loaded.result.failed()){report(loaded.result.getErrorMessage()+" Previous amp kept.");populatePlugins();return;}
        if(!ready())
        {startupPlugin=*loaded.source->pluginDescription();loadRig(std::move(loaded.source));persistSelection();return;}
        const bool reopen=ampSource->editorVisible();player.setProcessor(nullptr);
        auto result=amp::replace(graph,ampSource,ampStage,pedalStageId,cabinetStageId,std::move(loaded.source),retiredAmps);
        configureOutputDsp();attach();if(reopen)showEditor();populatePlugins();
        if(result.failed())report(result.getErrorMessage());
        else {displayedSource=ampSource->type();syncAmpUi();persistSelection();active.setText("Amp: "+ampName(),dontSendNotification);report("Active amp: "+vst3::label(*ampSource->pluginDescription()));}
        refresh();
    }
    void askSave()
    {
        if(!ready())return;
        const auto initial=name.getText();
        auto* prompt=new tonegrid::popup::Prompt("Name this preset. Saving replaces the selected slot.","Save",&initial);
        tonegrid::popup::launch("Save preset",*prompt,*this,true,0,ModalCallbackFunction::create(
            [safe=Component::SafePointer<RigHost>(this),content=Component::SafePointer<tonegrid::popup::Prompt>(prompt)](int result)
            {if(result==1&&safe&&content){safe->name.setText(content->name(),false);safe->saveSlot();}}));
        prompt->focusName();
    }
    Result renamePreset(int index,const String& value)
    {
        const auto result=rigStore.rename(index,value);
        if(result.wasOk()){reloadSlots(selected());report("Preset renamed.");}else report(result.getErrorMessage());return result;
    }
    Result deletePreset(int index)
    {
        const auto result=rigStore.remove(index);
        if(result.wasOk()){if(index<rig::slotCount)clearBinding(index);reloadSlots(selected());report("Preset deleted. Current sound kept.");}else report(result.getErrorMessage());return result;
    }
    void movePreset(int index,int direction)
    {
        const int target=index+direction;if((direction!=-1&&direction!=1)||target<0||target>=rigStore.capacity())return;
        const auto result=rigStore.swap(index,target);if(result.failed()){presetBrowser.warning(result.getErrorMessage());return;}
        int current=selected();if(current==index)current=target;else if(current==target)current=index;
        reloadSlots(current);selectSlot();persistSelection();presetBrowser.focusUser(target);report("Preset moved. MIDI assignments remain at their slot positions.");
    }
    void askRenamePreset(int index)
    {
        if(!rigStore.contains(index))return;
        const auto initial=rigStore.readName(index);
        auto* prompt=new tonegrid::popup::Prompt("Enter the new preset name.","Rename",&initial);
        auto& owner=presetDialog?static_cast<Component&>(*presetDialog.getComponent()):static_cast<Component&>(*this);
        tonegrid::popup::launch("Rename preset",*prompt,owner,true,0,ModalCallbackFunction::create(
            [safe=Component::SafePointer<RigHost>(this),content=Component::SafePointer<tonegrid::popup::Prompt>(prompt),index](int result)
            {if(result==1&&safe&&content){const auto renamed=safe->renamePreset(index,content->name());if(renamed.failed())safe->presetBrowser.warning(renamed.getErrorMessage());}}));
        prompt->focusName();
    }
    void askDeletePreset(int index)
    {
        if(!rigStore.contains(index))return;
        auto* prompt=new tonegrid::popup::Prompt(tonegrid::l10n::format("preset.delete_copy",{{"preset",String(index+1).paddedLeft('0',3)+" - "+rigStore.readName(index)}}),"Delete");
        auto& owner=presetDialog?static_cast<Component&>(*presetDialog.getComponent()):static_cast<Component&>(*this);
        tonegrid::popup::launch("Delete preset",*prompt,owner,true,0,ModalCallbackFunction::create(
            [safe=Component::SafePointer<RigHost>(this),index](int result)
            {if(result==1&&safe){const auto removed=safe->deletePreset(index);if(removed.failed())safe->presetBrowser.warning(removed.getErrorMessage());}}));
    }
    void showPresetBrowser()
    {
        if(presetDialog){presetDialog->toFront(true);return;}
        presetBrowser.setSize(850,650);presetDialog=tonegrid::popup::launch("Presets",presetBrowser,*this,false,400);
    }
    void showMissingCaptures(const String& preset,StringArray missing)
    {
        missing.removeEmptyStrings();missing.removeDuplicates(false);if(missing.isEmpty())return;
        cancelPluginValidation=true;if(pluginValidation.valid())pluginValidation.wait();
        delete missingCapturesDialog.getComponent();auto* view=new tonegrid::MissingCapturesView(preset,missing);
        auto& owner=presetDialog?static_cast<Component&>(*presetDialog.getComponent()):static_cast<Component&>(*this);
        missingCapturesDialog=tonegrid::popup::launch("Missing preset captures",*view,owner,true);
        missingCaptureView=view;missingApplicationCheck=true;
        if(!onlineUpdates.busy())startOnlineCheck(true,false,true);
    }
    void loadToneBank(size_t index)
    {
        presetRequests.cancel();
        ++cabinetSelectionGeneration;pendingHead.reset();
        if(!pedals||index>=toneBank.presets.size())return;
        const auto& preset=toneBank.presets[index];
        auto catalog=namCatalog;
        // A cold cache can still resolve the exact known path without scanning every model.
        if(!tonegrid::tonebank::resolveAmp(preset.amp,catalog))for(const auto& relative:{preset.amp.file,String("Factory/")+preset.amp.file})
        {namamp::Entry entry;if(namLibrary.read(namLibrary.folder().getChildFile(relative),entry).wasOk()&&entry.identity.sha256==preset.amp.sha256){catalog.entries.push_back(std::move(entry));break;}}
        auto plan=tonegrid::tonebank::prepare(preset,catalog,*pedals,appPaths.models());
        if(!plan.warnings.isEmpty()){showMissingCaptures(preset.title(),plan.warnings);presetBrowser.warning("Missing captures. Current sound kept.");return;}
        std::optional<tonegrid::cabinet::Prepared> plannedCabinet;
        if(plan.replaceAmp&&plan.state.cabinet)
        {auto prepared=prepareCabinet(*plan.state.cabinet);if(prepared.result.failed()){report(prepared.result.getErrorMessage()+" Current rig kept.");presetBrowser.warning(prepared.result.getErrorMessage());showMissingCaptures(preset.title(),{prepared.result.getErrorMessage()});return;}plannedCabinet.emplace(std::move(prepared));}
        std::unique_ptr<amp::AmpSource> replacement;
        if(plan.replaceAmp)
        {
            auto loaded=amp::loadNamAmp(namLibrary,plan.state.namAmp,namLite);
            if(loaded.result.wasOk())replacement=std::move(loaded.source);
            else{showMissingCaptures(preset.title(),{loaded.result.getErrorMessage()});presetBrowser.warning("Missing amp. Current sound kept.");return;}
        }
        const auto previousNormalization=outputSettings.ampNormalizationEnabled;const auto previousEnhanced=outputSettings.ampNormalizationEnhanced;
        player.setProcessor(nullptr);
        if(replacement&&!ready()){loadRig(std::move(replacement),false);if(!ready()){plan.replaceAmp=false;plan.warnings.add(tonegrid::tonebank::missingAmp(preset.amp));}}
        if(replacement)
        {
            const auto changed=amp::replace(graph,ampSource,ampStage,pedalStageId,cabinetStageId,std::move(replacement),retiredAmps);
            if(changed.failed()){plan.replaceAmp=false;plan.warnings.add(tonegrid::tonebank::missingAmp(preset.amp));}
        }
        for(int p=0;p<board::count;++p)pedals->set(p,plan.state.pedals[(size_t)p]);pedals->front.set(plan.state.input);
        if(plan.replaceAmp&&plannedCabinet){cabinetState=*plan.state.cabinet;installCabinet(std::move(*plannedCabinet),false);}
        outputSettings=*plan.state.outputDsp;
        if(!plan.replaceAmp){outputSettings.ampNormalizationEnabled=previousNormalization;outputSettings.ampNormalizationEnhanced=previousEnhanced;}
        syncInput();for(auto& card:cards)if(card)card->sync();syncOutputUi();syncAmpUi();attach();
        currentPresetId=Uuid().toString();currentPresetExtensions.clear();
        toneBankActive=true;midiController.cancelLearn();
        // Save targets the existing editable User Preset store, preferably an
        // empty slot selected without recalling it and changing this tone.
        for(int i=0;i<rigStore.capacity();++i)if(!rigStore.contains(i)){slot.setSelectedId(i+1,dontSendNotification);break;}
        name.setText(preset.title(),false);presetEntry.setButtonText(preset.artist+" / "+preset.song);
        active.setText("ToneBank: "+preset.title(),dontSendNotification);report("Loaded ToneBank: "+preset.title());
        const auto warning=plan.warnings.joinIntoString("\n");presetBrowser.warning(warning.isEmpty()?"Loaded: "+preset.title()+". Save creates a User Preset.":warning);
        for(const auto& message:plan.warnings)report(message);
        showMissingCaptures(preset.title(),plan.warnings);
        refresh();
    }

    bool ready()const{return ampSource && ampSource->loaded() && pedals;}
    int selected()const{return jmax(0,slot.getSelectedId()-1);}
    File preferences(const String& filename)const
    {auto f=bankDir.getChildFile(filename); return f.existsAsFile()?f:bankDir.getSiblingFile("Rigs-v2").getChildFile(filename);}
    void report(const String& text)
    {auto line=Time::getCurrentTime().toString(false,true)+"  "+text+"\n";latestLog.setText(line.trim().replaceCharacters("\r\n","  "),dontSendNotification); if(log.getTotalNumChars()>65536)log.setText(log.getText().substring(log.getTotalNumChars()-32768),false);log.moveCaretToEnd();log.insertTextAtCaret(line);if(bankDir.createDirectory().wasOk()){const auto file=bankDir.getChildFile("rig.log");if(file.getSize()>2*1024*1024)file.moveFileTo(bankDir.getChildFile("rig.previous.log"));file.appendText(line);}}
    String targetName(int i)const{return board::MidiController::targetName(i);}
    void refresh()
    {
        const bool native=displayedSource==amp::SourceType::Nam;
        pluginChoice.setVisible(!native);ampGui.setVisible(!native);scanPlugins.setVisible(!native);addPluginFolder.setVisible(!native);
        namChoice.setVisible(false);refreshNam.setVisible(false);ampPanel.setVisible(native);namFolderName.setVisible(false);namGain.setVisible(native);namVolume.setVisible(native);ampEqGear.setVisible(native);if(!native)delete ampEqDialog.getComponent();normalize.setVisible(native);normalizeHelp.setVisible(native);
        const bool namActive=ready()&&ampSource->type()==amp::SourceType::Nam;namGain.setEnabled(namActive);namVolume.setEnabled(namActive);ampEqGear.setEnabled(namActive);ampEqControls.setEnabled(namActive);namChoice.setEnabled(pedals!=nullptr);
        normalize.setEnabled(namActive);spreadControls.setEnabled(ready());spreadToggle.setEnabled(ready());spreadGear.setEnabled(ready());reverbToggle.setEnabled(ready());reverbGear.setEnabled(ready());
        cabinetPanel.setVisible(native);
        namSourceButton.setToggleState(native,dontSendNotification);vstSourceButton.setToggleState(!native,dontSendNotification);
        guitarIn.setEnabled(pedals!=nullptr);gateThreshold.setEnabled(pedals!=nullptr);gateToggle.setEnabled(pedals!=nullptr);
        path.setEnabled(!ampSource||!ampSource->loaded()); ampGui.setEnabled(pedals!=nullptr); slot.setEnabled(ready());presetEntry.setEnabled(pedals!=nullptr); save.setEnabled(ready());
        pluginChoice.setEnabled(pedals!=nullptr&&!pluginScan.valid()&&!pluginValidation.valid());scanPlugins.setEnabled(!pluginScan.valid()&&!pluginValidation.valid());addPluginFolder.setEnabled(!pluginScan.valid()&&!pluginValidation.valid());
        const bool saved=rigStore.contains(selected()); learn.setEnabled(midi&&ready()&&saved&&!toneBankActive&&selected()<rig::slotCount);
        learn.setButtonText(midiController.learningTarget()==selected()?"Cancel Learn":"MIDI Learn");clear.setEnabled(selected()<rig::slotCount&&midiController.assignments()[(size_t)selected()].valid());
        for(int v=0;v<board::count;++v)if(cards[(size_t)v]){int p=PedalCard::engineFor(v);if(p>=0)cards[(size_t)v]->midi(midiController.assignments()[(size_t)(board::pedalMidiTarget(p))],midiController.learningTarget()==board::pedalMidiTarget(p),midi!=nullptr);}
        if(pedalMidiView&&pedalMidiTarget>=0)pedalMidiView->update(midiController.assignments()[size_t(pedalMidiTarget)],midiController.learningTarget()==pedalMidiTarget,midi!=nullptr);
    }
    void reloadSlots(int selection)
    {
        slot.clear(dontSendNotification);
        const int presetCapacity=rigStore.capacity();
        for(int i=0;i<presetCapacity;++i){auto label=rigStore.contains(i)?rigStore.readName(i).trim():String("(empty)");slot.addItem(String(i+1).paddedLeft('0',3)+" - "+label,i+1);}
        slot.setSelectedId(selection+1,dontSendNotification);selectSlot();
        StringArray users;std::vector<bool> stored;for(int i=0;i<slot.getNumItems();++i){users.add(slot.getItemText(i));stored.push_back(rigStore.contains(i));}presetBrowser.setUsers(users,std::move(stored));
    }
    void selectSlot(){if(!rigStore.contains(selected()))currentPresetId=Uuid().toString();name.setText(rigStore.readName(selected()),false);presetEntry.setButtonText(slot.getText());refresh();}
    void startLearn(int target){midiController.startLearn(target,*this);}
    void showPedalMidiLearn(int pedal)
    {
        delete pedalMidiDialog.getComponent();midiController.cancelLearn();
        const auto target=board::pedalMidiTarget(pedal);pedalMidiTarget=target;
        auto* view=new tonegrid::PedalMidiLearnView(board::definition(pedal).displayName);pedalMidiView=view;
        const auto safe=Component::SafePointer<RigHost>(this);const auto panel=Component::SafePointer<tonegrid::PedalMidiLearnView>(view);
        view->onLearn=[safe,target]{if(safe&&safe->midi)safe->startLearn(target);};
        view->onClear=[safe,target]{if(safe)safe->clearBinding(target);};
        view->onClose=[safe,target]{if(safe){if(safe->midiController.learningTarget()==target)safe->midiController.cancelLearn();if(safe->pedalMidiDialog)safe->pedalMidiDialog->exitModalState(0);safe->refresh();}};
        pedalMidiDialog=tonegrid::popup::launch("Learn MIDI - "+String(board::definition(pedal).displayName),*view,*this,true,160,
            ModalCallbackFunction::create([safe,panel,target](int){if(safe&&safe->pedalMidiTarget==target&&safe->pedalMidiView.getComponent()==panel.getComponent()){if(safe->midiController.learningTarget()==target)safe->midiController.cancelLearn();safe->pedalMidiTarget=-1;safe->refresh();}}));
        if(midi)midiController.startLearn(target,*this);
        view->update(midiController.assignments()[size_t(target)],midiController.learningTarget()==target,midi!=nullptr);
    }
    void clearBinding(int target){midiController.clearBinding(target,*this);}
    bool storeMidiBindings(const board::Bindings& next)override
    {if(!rig::writeXml(board::encodeBindings(next),bankDir.getChildFile("midi.xml"))){report("Could not save MIDI assignments.");return false;}return true;}
    void reportMidi(const String& text)override{report(text);if(pedalMidiView)pedalMidiView->feedback(text);}
    void midiStateChanged()override{refresh();}
    void recallMidiRig(int target)override{recallSlot(target);}
    void toggleMidiPedal(int persistentId)override
    {
        if(!pedals)return;
        board::togglePedal(*pedals,persistentId);
        // The command has already executed. A card only reflects the new state.
        for(int v=0;v<board::count;++v)if(board::order[(size_t)v]==persistentId&&cards[(size_t)v])cards[(size_t)v]->sync();
    }
    void loadRig(std::unique_ptr<amp::AmpSource> prepared=nullptr,bool recallStored=true,bool connectAudio=true)
    {
        if((ampSource&&ampSource->loaded()) || !pendingBoard)return;
        const File file(path.getText().unquoted());amp::LoadResult loaded;
        if(prepared)loaded.source=std::move(prepared);
        else if(startupNam){auto native=amp::loadNamAmp(namLibrary,*startupNam,namLite);loaded.source=std::move(native.source);loaded.result=native.result;}
        else if(startupPlugin)
        {
            PluginDescription resolved;auto resolution=pluginCatalog.resolve(*startupPlugin,resolved);
            loaded=resolution.wasOk()?vst3::load(resolved):amp::LoadResult{nullptr,resolution,{}};
        }
        else loaded.result=Result::fail("Choose a NAM capture or open VST to select an installed plugin.");
        if(loaded.result.failed()){report(loaded.result.getErrorMessage());if(loaded.statusMessage.isNotEmpty())active.setText(loaded.statusMessage,dontSendNotification);return;}
        ampSource=std::move(loaded.source);
        const auto stages=tonegrid::core::buildGuitarGraph(graph,std::move(pendingBoard),*ampSource);
        if(!stages.connected){ampSource->unload();report("Audio graph connection failed. Restart the application.");refresh();return;}
        ampStage=stages.amp;pedalStageId=stages.pedal;outputStageId=stages.output;normalizationStageId=stages.normalization;cabinetStageId=stages.cabinet;
        cabinetNode=stages.cabinetProcessor;ampEqNode=stages.eq;normalizationNode=stages.normalize;spreadNode=stages.spread;modNode=stages.mod;delayNode=stages.delay;reverbNode=stages.reverb;configureOutputDsp();
        report("Sound chain prepared: 13 pedal slots -> "+ampName()+". Internal engine: 48000 Hz; device sample rate is adapted automatically.");
        if(!startupPlugin&&!startupNam&&bankDir.createDirectory().wasOk())bankDir.getChildFile("tonex-path.txt").replaceWithText(file.getFullPathName());
        displayedSource=ampSource->type();populatePlugins();syncAmpUi();
        active.setText(ampSource->type()==amp::SourceType::Nam?"Ready. Choose a local NAM AMP+CAB or select a saved rig.":"Ready. Open Amp to choose your tone, or select a saved rig.",dontSendNotification);
        if(recallStored&&rigStore.contains(selected()))recallSlot(selected());
        if(connectAudio){initialiseAudio();attach();}
        refresh();
    }
    #include "App/StartupHost.inl"
    void pollWelcome()
    {
        if(startupPresetPending||welcomeOffered||!isShowing()||ModalComponentManager::getInstance()->getNumModalComponents()!=0)return;
        welcomeOffered=true;calibrationOnboardingOffered=true;auto* view=new tonegrid::welcome::View();view->setSize(view->getWidth(),view->requiredHeight());
        view->onDismiss=[safe=Component::SafePointer<RigHost>(this)](bool suppress)
        {
            if(!safe)return;
            if(suppress){const auto saved=tonegrid::welcome::dismiss(safe->appPaths.betaWelcome());if(saved.failed())safe->report(saved.getErrorMessage());}
            if(safe->welcomeDialog)safe->welcomeDialog->closeButtonPressed();
        };
        const auto safe=Component::SafePointer<RigHost>(this);
        const auto body=Component::SafePointer<tonegrid::welcome::View>(view);
        view->onSettings=[safe,body]{if(safe&&body)safe->audioSettings(body->findParentComponentOfClass<DialogWindow>());};
        view->onCalibrate=[safe,body]{if(safe&&body)safe->showGuitarCalibration(body->findParentComponentOfClass<DialogWindow>());};
        welcomeDialog=tonegrid::popup::launch("Welcome to ToneGrid",*view,*this,true);
    }
    void showEditor()
    {
        if(!ready())return;auto result=ampSource->openEditor();if(result.failed())report(result.getErrorMessage());
    }
    bool initialiseAudio()
    {
        return hardwareEnabled&&initialiseAudioDevices();
    }
    bool initialiseAudioDevices()
    {
        audioInitialisationAttempted=true;
        if(!audioCallbackAttached){devices.addAudioCallback(&audioTap);audioCallbackAttached=true;}
        // A user selection can precede the manager's asynchronous change message.
        // Adopt that working device before considering an older saved choice.
        reconcileAudioDevice();if(audioReady)return true;
        auto xml=XmlDocument::parse(preferences("audio.xml"));auto error=devices.initialise(1,2,xml.get(),true);
        if(error.isNotEmpty()){audioReady=false;lastAudioError=error;report("Audio error: "+error);updateAudioSettingsStatus();return false;}
        auto setup=devices.getAudioDeviceSetup();
        const int inputChannel=jmax(0,setup.inputChannels.findNextSetBit(0));
        setup.inputChannels.clear();setup.inputChannels.setBit(inputChannel);setup.useDefaultInputChannels=false;
        error=devices.setAudioDeviceSetup(setup,true);
        if(error.isNotEmpty()){lastAudioError=error;report("Audio device setup: "+error);}
        reconcileAudioDevice();return audioReady;
    }
    void reconcileAudioDevice()
    {
        auto* device=devices.getCurrentAudioDevice();const bool open=device&&device->isOpen();
        if(open&&!audioCallbackAttached){devices.addAudioCallback(&audioTap);audioCallbackAttached=true;}
        const bool wasReady=audioReady;audioReady=open&&device->isPlaying();
        if(open&&!tonegrid::audio::DeviceRateProcessor::supported(device->getCurrentSampleRate()))
        {audioReady=false;lastAudioError="Unsupported sample rate. Choose 44100 Hz or 48000 Hz.";}
        if(open&&(device->getActiveInputChannels().isZero()||device->getActiveOutputChannels().isZero()))
        {audioReady=false;lastAudioError="Choose one guitar input and at least one output in Audio Settings.";}
        if(audioReady)
        {
            lastAudioError.clear();
            if(!wasReady)report("Audio device ready: "+device->getName());
        }
        else if(device&&device->getLastError().isNotEmpty())lastAudioError=device->getLastError();
        if(wasReady&&!audioReady)report("Audio device unavailable. Open Audio Settings to select or reconnect your interface.");
        updateAudioSettingsStatus();
    }
    String audioSettingsMessage() const
    {
        String message=audioReady?tonegrid::l10n::tr("audio.ready_help"):lastAudioError.isNotEmpty()?tonegrid::l10n::format("audio.failure",{{"error",lastAudioError}}):tonegrid::l10n::tr("audio.none_help");
#if JUCE_WINDOWS
            if(devices.getCurrentAudioDeviceType()!="ASIO")message+="\n"+tonegrid::l10n::tr("audio.asio_hint");
#endif
        return message;
    }
    void updateAudioSettingsStatus()
    {
        if(!audioDialog)return;auto* frame=audioDialog->getContentComponent();
        std::function<tonegrid::AudioSettingsPanel*(Component*)> find=[&](Component* c)->tonegrid::AudioSettingsPanel*{if(auto* panel=dynamic_cast<tonegrid::AudioSettingsPanel*>(c))return panel;for(auto* child:c->getChildren())if(auto* panel=find(child))return panel;return nullptr;};
        if(auto* panel=find(frame))
        {
            panel->setAudioStatus(audioSettingsMessage(),!audioReady&&lastAudioError.isNotEmpty());
        }
    }
    String diagnosticReport()
    {
        using namespace tonegrid::diagnostics;String text="Please describe what happened:\n\n\n"+systemSummary();
        text+="\nModels catalogue: "+String(appPaths.models().getChildFile("catalog.xml").existsAsFile()?"present":"MISSING — extract the complete ZIP")+"\nFactory manifest: "+String(appPaths.factoryAmps().getChildFile("manifest.json").existsAsFile()?"present":"missing")+"\nMP3 encoder: "+String(appPaths.encoder().existsAsFile()?"present":"missing — keep Tools beside ToneGrid")+"\n";
        text+="User folder: "+appPaths.user().getFullPathName()+"\nCustom VST3 folders: "+vst3Directory.getChildFile("scan-paths.txt").loadFileAsString()+"\nVST3 plugins indexed: "+String(int(pluginCatalog.plugins.size()))+"\n";
#if JUCE_WEB_BROWSER
        text+="Embedded browser: "+String(WebBrowserComponent::areOptionsSupported(tonegrid::platform::browserOptions(appPaths.browserProfile()))?"available":"unavailable — Windows requires Microsoft Edge WebView2 Runtime")+"\n";
#endif
        text+="\nAudio ready: "+String(audioReady?"yes":"no")+"\nProcessing paused: "+String(audioFault?"yes":"no")+"\nLast audio error: "+(lastAudioError.isEmpty()?String("none"):lastAudioError)+"\n";
        text+="Driver type: "+devices.getCurrentAudioDeviceType()+"\n";
        const auto setup=devices.getAudioDeviceSetup();
        text+="Selected input: "+setup.inputDeviceName+"\nSelected output: "+setup.outputDeviceName+"\nRequested rate: "+String(setup.sampleRate,0)+" Hz\nRequested buffer: "+String(setup.bufferSize)+" samples\n";
        if(auto* device=devices.getCurrentAudioDevice())
        {
            text+="Device: "+device->getName()+"\nOpen: "+String(device->isOpen()?"yes":"no")+"; playing: "+String(device->isPlaying()?"yes":"no")+"\n";
            text+="Device rate: "+String(device->getCurrentSampleRate(),0)+" Hz\nBuffer: "+String(device->getCurrentBufferSizeSamples())+" samples\nBit depth: "+String(device->getCurrentBitDepth())+"\n";
            text+="Input channels: "+device->getInputChannelNames().joinIntoString(", ")+"; active bits: "+device->getActiveInputChannels().toString(2)+"\n";
            text+="Output channels: "+device->getOutputChannelNames().joinIntoString(", ")+"; active bits: "+device->getActiveOutputChannels().toString(2)+"\n";
            text+="Latency: input "+String(device->getInputLatencyInSamples())+", output "+String(device->getOutputLatencyInSamples())+" samples\nDevice error: "+device->getLastError()+"\n";
        }
        else text+="Device: none\n";
        text+="Engine rate: 48000 Hz\nResampling: "+String(deviceProcessor.converting()?"yes":"no")+"\nCallback registered: "+String(audioCallbackAttached?"yes":"no")+"\n";
        StringArray drivers;for(auto* type:devices.getAvailableDeviceTypes())drivers.add(type->getTypeName());text+="Available driver types: "+drivers.joinIntoString(", ")+"\n";
        if(auto saved=XmlDocument::parse(preferences("audio.xml")))
            text+="Saved audio: "+saved->getStringAttribute("deviceType")+"; input "+saved->getStringAttribute("audioInputDeviceName")+"; output "+saved->getStringAttribute("audioOutputDeviceName")+"; rate "+saved->getStringAttribute("audioDeviceRate")+"\n";
        else text+="Saved audio: absent or unreadable\n";
        text+="\nCalibration enabled: "+String(calibrationSettings.enabled?"yes":"no")+"; completed: "+String(calibrationSettings.completed?"yes":"no")+"; needs verification: "+String(calibrationSettings.needsVerification?"yes":"no")+"\n";
        text+="Calibration profile: "+calibrationSettings.profileId+"; reference: "+(std::isfinite(calibrationSettings.referenceDbU)?String(calibrationSettings.referenceDbU,2)+" dBu":String("unknown"))+"\n";
        text+="Calibration device: "+calibrationSettings.deviceType+" / "+calibrationSettings.deviceName+"; channel "+String(calibrationSettings.channel+1)+"\n";
        text+="NAM Lite: "+String(namLite?"yes":"no")+"\nNormalization: "+normalize.getText()+"\nGraph ready: "+String(ready()?"yes":"no")+"\n";
        if(ampSource)text+="Amp engine: "+String(ampSource->type()==amp::SourceType::Nam?"NAM":"VST3")+"\n";
        if(const auto* native=dynamic_cast<const amp::NamAmp*>(ampSource.get()))text+="Capture: "+native->state().model.file.replaceCharacter('\\','/').fromLastOccurrenceOf("/",false,false)+"\n";
        text+="Board size: "+String(getWidth())+" x "+String(getHeight())+"\nWindow size: "+getTopLevelComponent()->getBounds().toString()+"\n";
        text+="\nRecent session log (last 32 lines):\n"+recentLog(log.getText())+"\n\nEND OF TONEGRID DIAGNOSTIC\n";
        return sanitize(text);
    }
    void showDiagnostic()
    {
        if(diagnosticDialog){diagnosticDialog->setVisible(true);diagnosticDialog->toFront(true);return;}
        auto* view=new tonegrid::diagnostics::View(diagnosticReport());view->launchEmail=diagnosticEmailLauncher;
        Component& owner=audioDialog?static_cast<Component&>(*audioDialog.getComponent()):static_cast<Component&>(*this);
        diagnosticDialog=tonegrid::popup::launch("Send Diagnostic",*view,owner,true,320);view->openDraft();
    }
    void attach()
    {
        auto* device=devices.getCurrentAudioDevice();
        if(reverbNode)reverbNode->stereoOutputAvailable(!device||device->getActiveOutputChannels().countNumberOfSetBits()>=2);
        if(spreadNode){const bool stereo=!device||device->getActiveOutputChannels().countNumberOfSetBits()>=2;spreadNode->stereoOutputAvailable(stereo);spreadControls.setTooltip(stereo?"Post-amp mono-to-stereo ADT doubler.":"Spread inactive: output device is mono.");}
        auto* wanted=audioReady && ready() && !audioFault && device && tonegrid::audio::DeviceRateProcessor::supported(device->getCurrentSampleRate()) ? &deviceProcessor : nullptr;
        if(player.getCurrentProcessor()!=wanted)player.setProcessor(wanted);
    }
    void showTuner()
    {
        if(tunerDialog){tunerDialog->toFront(true);return;}
        if(hardwareEnabled){if(!initialiseAudio())return;attach();}
        tunerControls.open();
        tunerDialog = new tonegrid::tuning::Window(tunerControls, *this);
        tunerDialog->enterModalState(true, nullptr, true);
        if(auto* dialog=tunerDialog.getComponent())ModalComponentManager::getInstance()->attachCallback(dialog,ModalCallbackFunction::create([safe=Component::SafePointer<RigHost>(this)](int){if(safe)safe->tunerControls.close();}));
    }
    void audioSettings(Component* owner=nullptr)
    {
        if(audioDialog){audioDialog->setVisible(true);audioDialog->toFront(true);return;}
        // Device failure must never block access to the controls needed to fix it.
        audioFault=false;reconcileAudioDevice();attach();
        if(hardwareEnabled&&!audioInitialisationAttempted)initialiseAudio();
        auto* panel=new tonegrid::AudioSettingsPanel(devices,namLite,calibrationSettings);
        panel->onLite=[safe=Component::SafePointer<RigHost>(this)](bool lite){return safe&&safe->setNamLite(lite);};
        panel->onSetup=[safe=Component::SafePointer<RigHost>(this),body=Component::SafePointer<tonegrid::AudioSettingsPanel>(panel)]{if(safe&&body)safe->showGuitarCalibration(body->findParentComponentOfClass<DialogWindow>());};
        panel->setAudioStatus(audioSettingsMessage(),!audioReady&&lastAudioError.isNotEmpty());
        audioDialog=tonegrid::popup::launch("Audio Settings",*panel,owner?*owner:*this,true);
        updateAudioSettingsStatus();
    }
    bool setCalibration(tonegrid::calibration::Settings value)
    {
        if(!tonegrid::calibration::valid(value))return false;
        if(!tonegrid::calibration::write(calibrationPreference,value)){report("Cannot save level calibration. Previous setting kept.");return false;}
        player.setProcessor(nullptr);
        calibrationSettings=value;
        if(pedals)pedals->setCalibration(value);
        configureOutputDsp();attach();
        updateCalibrationStatus();report(value.enabled?"NAM calibration ON: "+String(value.referenceDbU,2)+" dBu. Missing references remain unknown.":"NAM calibration OFF: no automatic level compensation.");return true;
    }
    bool setNamLite(bool lite)
    {
        if(lite==namLite)return true;
        // Build a replacement amp before touching the running graph or preference.
        amp::NamLoadResult replacement;auto* native=dynamic_cast<amp::NamAmp*>(ampSource.get());
        if(native&&native->supportsLite()){replacement=amp::loadNamAmp(namLibrary,native->state(),lite);if(replacement.result.failed()){report(replacement.result.getErrorMessage());return false;}}
        if(!tonegrid::namquality::write(qualityPreference,lite)){report("Cannot save NAM A2 Lite preference. Previous mode kept.");return false;}
        player.setProcessor(nullptr);int compatible=0;
        try
        {
            if(pedals)compatible=pedals->setNamLite(lite);
            if(replacement.source)
            {
                const auto changed=amp::replace(graph,ampSource,ampStage,pedalStageId,cabinetStageId,std::move(replacement.source),retiredAmps);
                if(changed.failed())throw std::runtime_error(changed.getErrorMessage().toStdString());
            }
            namLite=lite;
        }
        catch(const std::exception& error)
        {
            try{if(pedals)pedals->setNamLite(namLite);}catch(...){audioFault=true;}
            tonegrid::namquality::write(qualityPreference,namLite);attach();report("NAM quality change failed: "+String(error.what()));return false;
        }
        syncAmpUi();attach();report(String("NAM A2 ")+(lite?"Lite":"Full")+": applied to compatible models ("+String(compatible)+" pedal captures). Other models unchanged.");return true;
    }
    void saveSlot()
    {
        if(!ready())return;board::State s;s.presetId=currentPresetId;s.extensionXml=currentPresetExtensions;s.input=pedals->front.get();s.name=name.getText().trim();if(s.name.isEmpty()){report("Enter a rig name before saving.");return;}
        s.instrumentMode=pedals->instrumentMode();s.rackOrder=pedals->rackOrder();s.triComp=pedals->getTriComp();s.triDrive=pedals->getTriDrive();s.triDriveAssets=pedals->triDriveProcessor().assetState();
        s.sourceType=ampSource->type();if(s.sourceType==amp::SourceType::Nam)s.cabinet=cabinetState;s.genericVst3=s.sourceType==amp::SourceType::Vst3;
        s.outputDsp=outputSettings;s.outputDsp->expandedEffects=true;
        player.setProcessor(nullptr);
        try{if(auto* source=dynamic_cast<amp::NamAmp*>(ampSource.get()))s.namAmp=source->state();else ampSource->captureState(s.amp);for(int p=0;p<board::count;++p)s.pedals[(size_t)p]=pedals->get(p);for(int p=0;p<board::count;++p)if(board::isNam(p)&&s.pedals[size_t(p)].preset==board::presetCount(p))s.customPedalHashes[size_t(p)]=pedals->identity(p,s.pedals[size_t(p)].preset);}
        catch(...){attach();report("Plugin state capture failed; save cancelled.");return;}
        attach();
        const PluginDescription nativeDescription;const auto* description=ampSource->pluginDescription();
        const auto saved=rigStore.save(selected(),s,description?*description:nativeDescription,rigIdentities());
        if(saved.result.failed()){report(saved.result.getErrorMessage());return;}
        if(!saved.nameIndexSaved)report("Rig saved but name index could not be saved.");
        toneBankActive=false;report("Saved 13 pedals + "+ampName()+" in slot "+String(selected()+1));active.setText("Saved: "+s.name,dontSendNotification);reloadSlots(selected());persistSelection();
    }
    void recallSlot(int i)
    {
        if(!ready()||i<0||i>=board::RigStore::maximumPresets)return;
        ++cabinetSelectionGeneration;pendingHead.reset();
        board::State s;s.presetId=currentPresetId;s.extensionXml=currentPresetExtensions;PluginDescription wanted;auto result=rigStore.loadForSelection(i,rigIdentities(),s,wanted);
        if(result.failed()){report(result.getErrorMessage());if(result.getErrorMessage().containsIgnoreCase("capture"))showMissingCaptures(rigStore.readName(i),{result.getErrorMessage()});return;}
        applyRigState(std::move(s),wanted,i);
    }
    bool applyRigState(board::State s,const PluginDescription& wanted,int i,std::unique_ptr<amp::AmpSource> preparedAmp={},std::optional<tonegrid::cabinet::Prepared> preparedCabinet={},std::optional<std::vector<tonegrid::userpedals::Prepared>> preparedCustom={},bool usePreparedStartupAmp=false)
    {
        presetRequests.cancel();
        Result result=Result::ok();
        if(!validatedRecall)++pluginLoadGeneration;
        if(i>=0&&s.sourceType==amp::SourceType::Vst3&&!validatedRecall&&hardwareEnabled&&(!ampSource->pluginDescription()||board::ampCompatibility(wanted,*ampSource->pluginDescription(),s.genericVst3).failed()))
        {PluginDescription resolved;auto available=pluginCatalog.resolve(wanted,resolved);if(available.failed()){report(available.getErrorMessage()+" Current sound kept.");displayedSource=amp::SourceType::Vst3;syncAmpUi();return false;}requestPluginValidation(resolved,i,s.amp);return false;}
        if(s.sourceType==amp::SourceType::Vst3&&ampSource->pluginDescription()&&wanted.version!=ampSource->pluginDescription()->version)report("Plugin version changed. Trying the saved settings with the same plugin identity; verify the resulting sound.");
        std::optional<tonegrid::cabinet::Prepared> recalledCabinet=std::move(preparedCabinet);
        if(!recalledCabinet&&s.cabinet&&!s.cabinet->empty()){auto prepared=prepareCabinet(*s.cabinet);if(prepared.result.failed()){report(prepared.result.getErrorMessage()+" Current rig kept.");showMissingCaptures(s.name,{prepared.result.getErrorMessage()});return false;}recalledCabinet.emplace(std::move(prepared));}
        auto customModels=preparedCustom?std::move(*preparedCustom):preparePersonalRecall(s);
        if(!s.missingCaptureDiagnostics.isEmpty()){showMissingCaptures(s.name,s.missingCaptureDiagnostics);return false;}
        std::unique_ptr<amp::AmpSource> replacement=std::move(preparedAmp);
        if(s.sourceType==amp::SourceType::Nam&&!replacement&&!usePreparedStartupAmp)
        {auto loaded=amp::loadNamAmp(namLibrary,s.namAmp,namLite);if(loaded.result.failed()){const auto message="NAM amp: "+s.namAmp.model.file+"\n"+loaded.result.getErrorMessage();report(message+" Current rig kept.");auto missing=s.missingCaptureDiagnostics;missing.add(message);showMissingCaptures(s.name,missing);return false;}replacement=std::move(loaded.source);}
        else if(s.sourceType==amp::SourceType::Vst3&&(!ampSource->pluginDescription()||board::ampCompatibility(wanted,*ampSource->pluginDescription(),s.genericVst3).failed()))
        {
            amp::LoadResult loaded;
            if(s.genericVst3)
            {
                PluginDescription resolved;auto resolution=pluginCatalog.resolve(wanted,resolved);
                if(resolution.failed()){report(resolution.getErrorMessage()+" Current rig kept.");return false;}
                resolved.version=wanted.version;loaded=loadValidatedPlugin(resolved,&s.amp);
            }
            else loaded=amp::loadTonex(File(wanted.fileOrIdentifier));
            if(loaded.result.failed()){report(loaded.result.getErrorMessage()+" Current rig kept.");return false;}
            result=board::ampCompatibility(wanted,*loaded.source->pluginDescription(),s.genericVst3);
            if(result.failed()){report(result.getErrorMessage()+" Current rig kept.");return false;}
            replacement=std::move(loaded.source);
            if(!s.genericVst3)try{replacement->restoreState(s.amp);}catch(...){report("TONEX state restore failed; current rig kept.");return false;}
        }
        player.setProcessor(nullptr);const bool reopen=ampSource->editorVisible();ampSource->closeEditor();
        if(replacement)
        {
            result=amp::replace(graph,ampSource,ampStage,pedalStageId,cabinetStageId,std::move(replacement),retiredAmps);
            if(result.failed()){attach();if(reopen)showEditor();report(result.getErrorMessage());return false;}
        }
        else if(!(usePreparedStartupAmp&&s.sourceType==amp::SourceType::Nam))
        {
            bool recoveryFailed=false;result=amp::restoreWithRollback(*ampSource,s.amp,recoveryFailed);
            if(result.failed()){if(recoveryFailed)audioFault=true;attach();if(reopen)showEditor();report(result.getErrorMessage()+" Pedalboard unchanged.");return false;}
        }
        for(int p=0;p<board::count;++p)if(s.customPedalHashes[size_t(p)].isNotEmpty())pedals->installCustom(p,std::move(customModels[size_t(p)].model));
        for(int p=0;p<board::count;++p)pedals->set(p,s.pedals[(size_t)p]);pedals->front.set(s.input);
        pedals->setInstrumentMode(s.instrumentMode);pedals->setRackOrder(s.rackOrder);pedals->setTriComp(s.triComp);pedals->setTriDrive(s.triDrive);
        setInstrumentMode(s.instrumentMode);
        syncInput();for(auto& card:cards)if(card)card->sync();
        if(s.cabinet){cabinetState=*s.cabinet;if(recalledCabinet)installCabinet(std::move(*recalledCabinet),false);}
        currentPresetId=s.presetId;currentPresetExtensions=s.extensionXml;
        outputSettings=s.outputDsp.value_or(tonegrid::output::legacySettings());syncOutputUi();
        attach();if(reopen&&ampSource->type()==amp::SourceType::Vst3)showEditor();midiController.cancelLearn();if(i>=0)slot.setSelectedId(i+1,dontSendNotification);name.setText(s.name,false);
        displayedSource=ampSource->type();syncAmpUi();persistSelection();
        toneBankActive=false;presetEntry.setButtonText(i>=0?slot.getText():s.name);
        active.setText("Recalled: "+s.name,dontSendNotification);report(i>=0?"Restored 13 pedals + "+ampName()+" from slot "+String(i+1):i==-1?"Restored the Record session tone.":"Applied the selected ToneBank sound.");refresh();
        for(const auto& diagnostic:s.missingCaptureDiagnostics)report(diagnostic);
        showMissingCaptures(s.name,s.missingCaptureDiagnostics);
        return true;
    }
    #include "App/RecordIntegration.inl"
    void scanMidi()
    {
        if(!hardwareEnabled){midiDevice.clear(dontSendNotification);tonegrid::l10n::item(midiDevice,"MIDI input: Off",1);midiDevice.setSelectedId(1,dontSendNotification);midiDevice.setTooltip(midiDevice.getText());refresh();return;}
        auto previous=midi?midi->getIdentifier():preferences("midi-input.txt").loadFileAsString();if(midi)midi->stop();midi.reset();midiController.resetAfterInputStopped();
        midiDevices=MidiInput::getAvailableDevices();midiDevice.clear(dontSendNotification);tonegrid::l10n::item(midiDevice,"MIDI input: Off",1);int id=1;
        for(int i=0;i<midiDevices.size();++i){midiDevice.addItem(midiDevices[i].name,i+2);if(midiDevices[i].identifier==previous)id=i+2;}
        midiDevice.setSelectedId(id,dontSendNotification);openMidi(false);
    }
    void openMidi(bool persist=true)
    {
        midiDevice.setTooltip(midiDevice.getText());
        if(midi)midi->stop();midi.reset();midiController.resetAfterInputStopped();int i=midiDevice.getSelectedId()-2;
        if(isPositiveAndBelow(i,midiDevices.size())){midi=MidiInput::openDevice(midiDevices[i].identifier,this);if(midi){midi->start();report("MIDI: "+midiDevices[i].name);}else report("MIDI input busy or unavailable.");}
        if(persist&&bankDir.createDirectory().wasOk())bankDir.getChildFile("midi-input.txt").replaceWithText(isPositiveAndBelow(i,midiDevices.size())?midiDevices[i].identifier:String());refresh();
    }
    void handleIncomingMidiMessage(MidiInput*,const MidiMessage& message)override
    {
        midiController.enqueue(message);
    }
    void changeListenerCallback(ChangeBroadcaster*)override{audioSettingsDirty.store(true);}
    void exportRecording()
    {
        if(recorder.state()!=tonegrid::recording::Mode::ready||recordingChooser)return;
        const auto suggested=File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("ToneGrid-"+Time::getCurrentTime().formatted("%Y%m%d-%H%M%S")+".mp3");
        recordingChooser=std::make_unique<FileChooser>("Export recording as MP3",suggested,"*.mp3",true);
        recordingControls.setChoosingFile(true);
        recordingChooser->launchAsync(FileBrowserComponent::saveMode|FileBrowserComponent::canSelectFiles|FileBrowserComponent::warnAboutOverwriting,
            [safe=Component::SafePointer<RigHost>(this)](const FileChooser& chooser)
            {
                if(!safe)return;const auto chosen=chooser.getResult();safe->recordingControls.setChoosingFile(false);
                if(chosen!=File{}&&!safe->recorder.exportMp3(chosen.withFileExtension("mp3")))safe->report("The recording is not ready for export.");
                safe->recordingChooser.reset();
            });
    }
    void persistAudioSettings()
    {
        if(!audioReady||!audioSettingsDirty.exchange(false))return;
        if(auto xml=devices.createStateXml())
        {
            const auto text=xml->toString();
            if(text==lastAudioState)return;
            if(rig::writeXml(*xml,bankDir.getChildFile("audio.xml")))lastAudioState=text;
            else audioSettingsDirty.store(true);
        }
    }
    void advanceToolbarHints(Point<int> mouse,double dt,bool includeCards=true)
    {
        const bool keepPreset=save.hasKeyboardFocus(true)||learn.hasKeyboardFocus(true)||save.isMouseButtonDown()||learn.isMouseButtonDown()||midiController.learningTarget()>=0;
        presetActionOpacity=tonegrid::motion::fade(presetActionOpacity,sections[1].expanded(7,4).contains(mouse)||keepPreset,dt);
        midiRefreshOpacity=tonegrid::motion::fade(midiRefreshOpacity,sections[5].expanded(7,4).contains(mouse)||rescan.hasKeyboardFocus(true)||rescan.isMouseButtonDown(),dt);
        save.setAlpha(presetActionOpacity);learn.setAlpha(presetActionOpacity);rescan.setAlpha(midiRefreshOpacity);
        if(includeCards){ampPanel.advanceHover(ampPanel.getBounds().contains(mouse),dt);cabinetPanel.advanceHover(cabinetPanel.getBounds().contains(mouse),dt);}
    }
    std::array<Rectangle<int>,4> recordingActionBounds()const
    {
        const auto r=recordingControls.actionBounds();const auto p=getLocalPoint(&recordingControls,Point<int>{});
        return {r[0].translated(p.x,p.y),r[1].translated(p.x,p.y),r[2].translated(p.x,p.y),getLocalArea(loopControls.getParentComponent(),loopControls.getBounds()).withSizeKeepingCentre(jmin(46,loopControls.getWidth()),jmin(46,loopControls.getHeight()))};
    }
    Rectangle<int> recordingHintArea()const
    {
        const auto actions=recordingActionBounds();auto area=actions[0];
        for(const auto& r:actions)area=area.getUnion(r);
        return area.withRight(pedalArea.getRight()).expanded(7,5);
    }
    void timerCallback()override
    {
        pollPresetPreparation();
        const auto motionTime=Time::getMillisecondCounterHiRes()*.001;
        const auto motionDt=lastMotionTime>0?motionTime-lastMotionTime:1./30.;lastMotionTime=motionTime;
        if(isShowing())
        {
            const auto mouse=getMouseXYRelative();advanceToolbarHints(mouse,motionDt,false);
            const auto hint=tonegrid::motion::fade(recordingHintOpacity,!jamActive&&!learnActive&&!recordActive&&recordingHintArea().contains(mouse),motionDt);
            if(hint!=recordingHintOpacity){recordingHintOpacity=hint;playModule.contentView().setHintOpacity(hint);}
            guitarIn.advance(sections[2].expanded(7,4).contains(mouse),motionDt);
            gateThreshold.advance(sections[3].expanded(7,4).contains(mouse),motionDt);
            for(auto* tab:{&playTab,&jamTab,&learnTab})tab->animate(motionTime);
            if(!jamActive&&!learnActive&&!recordActive)
            {
                for(auto& card:cards)if(card&&card->isVisible())card->animate(motionTime);
                if(playModule.contentView().triCompCard&&playModule.contentView().triCompCard->isVisible())playModule.contentView().triCompCard->animate();
                if(playModule.contentView().triDriveCard&&playModule.contentView().triDriveCard->isVisible())playModule.contentView().triDriveCard->animate();
            }
        }
        recordingControls.setAudioAvailable(player.getCurrentProcessor()!=nullptr&&devices.getCurrentAudioDevice()!=nullptr);
        const auto recordingMessage=recorder.takeMessage();if(recordingMessage.isNotEmpty())report(recordingMessage);
        pollNamLibrary();pollCabinetLoad();pollCabinetRefresh();pollPersonalPedal();pollPersonalLibrary();
        loopControls.sync(player.getCurrentProcessor()!=nullptr&&devices.getCurrentAudioDevice()!=nullptr&&recorder.state()!=tonegrid::recording::Mode::playing);
        recordingControls.setLoopActive(looper.state()==tonegrid::looping::Mode::playing||looper.state()==tonegrid::looping::Mode::recording);
        pollOnlineUpdates();pollEditableContent();
        if(hardwareEnabled)pollWelcome();
        pollCalibration();
        if(bankLoading.valid()&&bankLoading.wait_for(std::chrono::seconds(0))==std::future_status::ready)
        {
            auto bank=bankLoading.get();toneBank=std::move(bank.bank);
            presetBrowser.setBank(toneBank,bank.result.failed()?bank.result.getErrorMessage():bank.warning);
            if(bank.result.failed())report(bank.result.getErrorMessage());else if(bank.warning.isNotEmpty())report(bank.warning);
        }

        retiredAmps.collect();
        if(pluginValidation.valid()&&pluginValidation.wait_for(std::chrono::seconds(0))==std::future_status::ready)
        {
            auto checked=pluginValidation.get();scanPlugins.setButtonText("Rescan VST3");
            if(activeValidationGeneration!=pluginLoadGeneration)report("Previous plugin request cancelled; current selection kept.");
            else if(checked.result.failed()){report(checked.result.getErrorMessage());auto* prompt=new tonegrid::popup::Prompt(checked.result.getErrorMessage(),"Close");tonegrid::popup::launch("Plugin could not load",*prompt,*this,true);}
            else if(checked.recall>=0){ScopedValueSetter<bool> allow(validatedRecall,true);recallSlot(checked.recall);}
            else {for(size_t i=0;i<pluginCatalog.plugins.size();++i)if(vst3::Identity::of(pluginCatalog.plugins[i])==vst3::Identity::of(checked.description)){pluginChoice.setSelectedId(int(i)+1,dontSendNotification);selectPlugin(true);break;}}
            refresh();
        }
        if(!startupProblemShown&&startupProblem.isNotEmpty()&&isShowing()&&ModalComponentManager::getInstance()->getNumModalComponents()==0)
        {startupProblemShown=true;auto* prompt=new tonegrid::popup::Prompt(startupProblem,"Close");tonegrid::popup::launch("ToneGrid setup",*prompt,*this,true);}
        if(pluginScan.valid())
        {
            if(pluginScan.wait_for(std::chrono::seconds(0))==std::future_status::ready)
            {
                auto scanned=pluginScan.get();
                if(scanned.result.failed())report(scanned.result.getErrorMessage()+" Previous VST3 cache kept.");
                else
                {
                    if(!cacheStore.write(scanned.catalog))report("Plugins found, but the cache could not be saved. The list is available for this session; check your user-folder permissions.");
                    pluginCatalog=std::move(scanned.catalog);report("VST3 scan: "+String((int)pluginCatalog.plugins.size())+" plugins, "+String((int)pluginCatalog.failures.size())+" failed bundles.");
                    for(const auto& failure:pluginCatalog.failures)report(failure.location+": "+failure.reason);
                    populatePlugins();
                }
                scanPlugins.setButtonText("Rescan VST3");refresh();
            }
            else scanPlugins.setTooltip("Scanning VST3: "+String(scanProgress.completed.load())+" / "+String(scanProgress.total.load()));
        }
        if(loading.valid()&&loading.wait_for(std::chrono::seconds(0))==std::future_status::ready)
        {
            auto r=loading.get();if(r.result.failed()){report(r.result.getErrorMessage());startupProblem="The pedalboard could not load: "+r.result.getErrorMessage()+"\nExtract the complete ZIP, keeping the Models folder beside ToneGrid.exe.";active.setText("Pedalboard unavailable. Extract the complete ZIP.",dontSendNotification);if(startupPresetPending)finishStartupFeedback();}
            else
            {
                pendingBoard=std::move(r.processor);pedals=pendingBoard.get();if(r.lite!=namLite)pedals->setNamLite(namLite);pedals->setCalibration(calibrationSettings);
                for(int v=0;v<board::count;++v){auto& c=cards[(size_t)v];c=std::make_unique<PedalCard>(v,*pedals);int p=PedalCard::engineFor(v);if(p>=0){c->onLearn=[this,p]{showPedalMidiLearn(p);};c->onClear=[this,p]{clearBinding(board::pedalMidiTarget(p));};c->onAddCapture=[this,p](bool t3){addPersonalPedal(p,t3);};c->onDeleteCapture=[this,p](const String& hash){deletePersonalPedal(p,hash);};c->onSelectCapture=[this,p](const String& hash){const auto entry=userPedals.entry(p,hash);loadPersonalPedal(p,userPedals.file(p,hash),hash,entry.label,entry.sourceUrl,false);};}playModule.contentView().addAndMakeVisible(*c);c->setVisible(!jamActive&&!learnActive&&!recordActive);}
                playModule.contentView().triCompCard=std::make_unique<tonegrid::play::BassRackCard>(*pedals,tonegrid::play::BassRackUnit::TriComp);
                playModule.contentView().triDriveCard=std::make_unique<tonegrid::play::BassRackCard>(*pedals,tonegrid::play::BassRackUnit::TriDrive);
                playModule.contentView().triCompCard->onOpenEditor=[this](auto u){showBassRackEditor(u);};
                playModule.contentView().triDriveCard->onOpenEditor=[this](auto u){showBassRackEditor(u);};
                playModule.contentView().addChildComponent(*playModule.contentView().triCompCard);
                playModule.contentView().addChildComponent(*playModule.contentView().triDriveCard);
                playModule.contentView().setInstrumentMode(pedals->instrumentMode(),!jamActive&&!learnActive&&!recordActive);
                refreshPersonalPedals();syncInput();
                resized();refresh();active.setText(startupPresetPending?"Preparing your presets...":startupNam?"Loading local NAM amp...":"Loading TONEX...",dontSendNotification);report("Loaded 151 NAM captures across nine pedals and the existing algorithmic presets.");report(startupPresetPending?"Preparing User Presets and amp source.":startupNam?"Loading local NAM amp automatically.":"Loading TONEX automatically.");if(!startupPresetPending)loadRig();
            }
        }
        pollStarterPresets();
        midiController.drain(*this);reconcileAudioDevice();
        if(!audioDialog)persistAudioSettings();
        auto* native=dynamic_cast<amp::NamAmp*>(ampSource.get());
        if(deviceProcessor.fault.exchange(false)||(pedals&&pedals->fault.exchange(false))||(native&&native->takeFault())){audioFault=true;report("Audio paused after a processing error. Open Audio settings to retry.");}
        attach();if(pedals)meter.update(pedals->front.takePeak());outputMeter.update(audioTap.takeOutputPeak());
        if(++statusTicks>=15)
        {
            statusTicks=0;String text=player.getCurrentProcessor()?"AUDIO ACTIVE  |  ":audioFault||lastAudioError.isNotEmpty()?"AUDIO ERROR - open Audio settings  |  ":"Waiting for audio - open Audio settings  |  ";
            if(midiController.learningTarget()>=0)text+="Learning "+targetName(midiController.learningTarget());else text+="Preset "+String(selected()+1)+"  |  MIDI: "+(midi?midi->getName():String("Off"));
            if(audioReady){text+="  |  CPU "+String(devices.getCpuUsage()*100,1)+"%";if(auto* dev=devices.getCurrentAudioDevice())text+="  |  "+String(dev->getCurrentSampleRate(),0)+" Hz / "+String(dev->getCurrentBufferSizeSamples())+" samples";}
            status.setText(text,dontSendNotification);
        }
    }
};
class MainWindow final:public DocumentWindow
{
public:
    explicit MainWindow(const File& applicationData=tonegrid::paths::applicationDataRoot(),bool useHardware=true,bool useBundledAssetsForTest=false,bool liveBrowsersForValidation=false)
        :DocumentWindow(String("ToneGrid ")+JUCE_APPLICATION_VERSION_STRING+(useHardware?" - Guitar pedalboard":" - isolated Mac validation (no audio IO)"),Colour(0xff14871c),allButtons)
    {
        setUsingNativeTitleBar(true);setIcon(ImageCache::getFromMemory(ToneGridApplicationAssets::ToneGrid512_png,ToneGridApplicationAssets::ToneGrid512_pngSize));
        setContentOwned(new tonegrid::ui::ScalableBoard(std::make_unique<RigHost>(applicationData,useHardware,useBundledAssetsForTest,liveBrowsersForValidation)),true);
        setResizable(true,false);setResizeLimits(240,180,32768,32768);
#if JUCE_MAC
        workAreaConstrainer.setSizeLimits(240,180,32768,32768);setConstrainer(&workAreaConstrainer);
#endif
        if(const auto* display=Desktop::getInstance().getDisplays().getPrimaryDisplay())setBounds(tonegrid::ui::ScalableBoard::initialBounds(display->userArea));
        else centreWithSize(1024,640);
        setVisible(true);
        // Native frame dimensions become available only once the peer exists.
        if(auto* peer=getPeer())if(const auto frame=peer->getFrameSizeIfPresent())
            if(const auto* display=Desktop::getInstance().getDisplays().getDisplayForRect(getBounds()))
                setBounds(frame->subtractedFrom(frame->addedTo(getBounds()).constrainedWithin(display->userArea)));
    }
    ~MainWindow() override {
#if JUCE_MAC
        setConstrainer(nullptr);
#endif
    }
    RigHost& host() const{return static_cast<RigHost&>(static_cast<tonegrid::ui::ScalableBoard*>(getContentComponent())->content());}
    void closeButtonPressed()override{JUCEApplication::getInstance()->systemRequestedQuit();}
#if JUCE_MAC
private:tonegrid::ui::WorkAreaConstrainer workAreaConstrainer;
#endif
};
class Application final:public JUCEApplication
{
public:
    const String getApplicationName()override{return "ToneGrid";}
    const String getApplicationVersion()override{return JUCE_APPLICATION_VERSION_STRING;}
    bool moreThanOneInstanceAllowed()override{return getCommandLineParameters().startsWith("--");}
    void initialise(const String& args)override
    {
        const auto tokens=StringArray::fromTokens(args,true);
        if(args=="--tone3000-native-url-test")
        {const auto output=File::getSpecialLocation(File::currentExecutableFile).getSiblingFile("Tone3000NativeUrlTests/results.txt");tone3000BrowserProbe=std::make_unique<Tone3000NativeBrowserProbe>(output,[this](bool ok){setApplicationReturnValue(ok?0:1);quit();});return;}
        if(tokens.size()>=1&&(tokens[0]=="--mac-validation-preview"||tokens[0]=="--mac-browser-validation-preview"||tokens[0]=="--ir139-preview"))
        {
            if(tokens.size()>2){setApplicationReturnValue(1);quit();return;}
            const auto parent=File::getSpecialLocation(File::tempDirectory);
            uiTestRoot=tokens.size()==2?File(tokens[1].unquoted()):parent.getChildFile("ToneGrid-139-"+Uuid().toString());
            const auto marker=uiTestRoot.getChildFile(".tonegrid139-preview");
            if(uiTestRoot.getParentDirectory()!=parent||!uiTestRoot.getFileName().startsWith("ToneGrid-139-")
                ||(uiTestRoot.exists()&&!marker.existsAsFile())||uiTestRoot.createDirectory().failed()
                ||!marker.replaceWithText("Isolated ToneGrid preview; hardware and MIDI disabled.\n"))
            {setApplicationReturnValue(1);quit();return;}
            keepValidationProfile=tokens.size()==2;
            window=std::make_unique<MainWindow>(uiTestRoot,false,true,tokens[0]=="--mac-browser-validation-preview");return;
        }
        if(tokens.size()==3&&tokens[0]=="--validate-vst3-worker")
        {const bool ok=vst3::validationWorker(File(tokens[1].unquoted()),File(tokens[2].unquoted()));setApplicationReturnValue(ok?0:1);quit();return;}
        if(tokens.size()==3&&tokens[0]=="--scan-vst3-worker")
        {const bool ok=vst3::scanWorker(File(tokens[1].unquoted()),File(tokens[2].unquoted()));setApplicationReturnValue(ok?0:1);quit();return;}
        if(args=="--record141-ui-test")
        {
            uiTestRoot=File::getSpecialLocation(File::tempDirectory).getChildFile("ToneGrid-record141-host-"+Uuid().toString());
            uiTestHost=std::make_unique<RigHost>(uiTestRoot,false,true);uiTestHost->setSize(1660,940);
            tone3000TestAfterDelay(100,[this]{finishRecord141(0);});return;
        }
        if(args=="--startup143-ui-test"||args=="--startup143-existing-ui-test")
        {
            uiTestRoot=File::getSpecialLocation(File::tempDirectory).getChildFile("ToneGrid-startup143-"+Uuid().toString());
            if(args=="--startup143-existing-ui-test")
            {
                const auto paths=tonegrid::paths::installed(uiTestRoot);const auto store=board::RigStore::forApplicationData(uiTestRoot);
                const auto source=paths.starterPresets().getChildFile("slot-002.tonexrig");const auto xml=XmlDocument::parse(source);
                const auto* amp=xml?xml->getChildByName("AmpSource"):nullptr;
                if(!amp){setApplicationReturnValue(1);quit();return;}
                store.directory().createDirectory();source.copyFileTo(store.slotFile(0));store.nameFile(0).replaceWithText(xml->getStringAttribute("name"));
                store.directory().getChildFile("starter-presets-sha256.txt").replaceWithText(SHA256(paths.starterPresets().getChildFile("manifest.json")).toHexString());
                const auto relative=amp->getStringAttribute("file");const auto target=namamp::libraryForApplicationData(uiTestRoot).getChildFile(relative);
                target.getParentDirectory().createDirectory();
                if(!paths.factoryAmps().getChildFile(relative).copyFileTo(target)){setApplicationReturnValue(1);quit();return;}
                for(const auto& art:paths.factoryAmps().getChildFile(relative).getParentDirectory().findChildFiles(File::findFiles,false,"thumbnail.*"))art.copyFileTo(target.getSiblingFile(art.getFileName()));
                const auto* cab=xml->getChildByName("CabinetIR");
                if(!cab){setApplicationReturnValue(1);quit();return;}
                const auto cabRelative=cab->getStringAttribute("file");const auto cabTarget=paths.user().getChildFile("CabinetIR").getChildFile(cabRelative);
                cabTarget.getParentDirectory().createDirectory();
                if(!paths.factoryCabinetIR().getChildFile(cabRelative).copyFileTo(cabTarget)){setApplicationReturnValue(1);quit();return;}
                for(const auto& art:paths.factoryCabinetIR().getChildFile(cabRelative).getParentDirectory().findChildFiles(File::findFiles,false,"thumbnail.*"))art.copyFileTo(cabTarget.getSiblingFile(art.getFileName()));
            }
            window=std::make_unique<MainWindow>(uiTestRoot,false,true);
            window->host().configureStartup143Probe(args=="--startup143-existing-ui-test");
            tone3000TestAfterDelay(10,[this]{finishStartup143(0);});return;
        }
        if(args=="--factory-refinements142-ui-test")
        {
            uiTestRoot=File::getSpecialLocation(File::tempDirectory).getChildFile("ToneGrid-refinements142-"+Uuid().toString());
            const auto retired=uiTestRoot.getChildFile("ToneGridIR");retired.createDirectory();retired.getChildFile("old-preset.txt").replaceWithText("Do not import this retired profile");
            uiTestHost=std::make_unique<RigHost>(uiTestRoot,false,true);uiTestHost->setSize(1660,940);
            legacyProfileCleanup.start(uiTestRoot);
            tone3000TestAfterDelay(100,[this]{finishFactoryRefinements142(0);});return;
        }
        if(args=="--ir-prototype-test")
        {
            uiTestRoot=File::getSpecialLocation(File::tempDirectory).getChildFile("ToneGrid-prototype-"+Uuid().toString());
            uiTestHost=std::make_unique<RigHost>(uiTestRoot,false,true);uiTestHost->setSize(1660,940);
            tone3000TestAfterDelay(100,[this]{finishIRPrototypeTest(0);});return;
        }
        if(args=="--record-video-browser-test")
        {const auto output=File::getSpecialLocation(File::currentExecutableFile).getSiblingFile("record-video-native.txt");recordVideoProbe=std::make_unique<RecordVideoBrowserProbe>(output,[this](bool ok){setApplicationReturnValue(ok?0:1);quit();});return;}
        if(args=="--jam131-browser-test")
        {const auto output=File::getSpecialLocation(File::currentExecutableFile).getSiblingFile("jam131-native-browser.txt");jamBrowserProbe=std::make_unique<Jam131BrowserProbe>(output,[this](bool ok){setApplicationReturnValue(ok?0:1);quit();});return;}
        if(args=="--learn132-browser-test")
        {const auto output=File::getSpecialLocation(File::currentExecutableFile).getSiblingFile("learn132-native-browser.txt");learnBrowserProbe=std::make_unique<Learn132BrowserProbe>(output,[this](bool ok){setApplicationReturnValue(ok?0:1);quit();});return;}
        if(args=="--private-beta125-ui-test"||args=="--starter133-legacy-ui-test"||args=="--starter133-existing-ui-test")
        {
            const auto exe=File::getSpecialLocation(File::currentExecutableFile);uiTestRoot=File::getSpecialLocation(File::tempDirectory).getChildFile("ToneGrid-private-beta-"+Uuid().toString());
            if(args!="--private-beta125-ui-test")
            {
                const auto paths=tonegrid::paths::installed(uiTestRoot);
                paths.ampSelection().getParentDirectory().createDirectory();
                paths.ampSelection().replaceWithText("<AmpSelection version=\"1\" type=\"VST3\"/>");
                if(args=="--starter133-legacy-ui-test")
                {
                    const auto archive=uiTestRoot.getChildFile("ToneGrid/Legacy/Rigs-v3/slot-001.tonexrig");
                    archive.getParentDirectory().createDirectory();archive.replaceWithText("<PedalRig version=\"3\" name=\"Mayer Clean\"/>");
                }
                else
                {
                    const auto store=board::RigStore::forApplicationData(uiTestRoot);store.directory().createDirectory();
                    for(int i=0;i<3;++i)
                    {
                        const auto source=paths.starterPresets().getChildFile(store.slotFile(i).getFileName());
                        auto xml=XmlDocument::parse(source);if(!xml){setApplicationReturnValue(1);quit();return;}
                        if(i==0)xml->setAttribute("name",xml->getStringAttribute("name")+" / My edit");
                        store.slotFile(i).replaceWithText(xml->toString());store.nameFile(i).replaceWithText(xml->getStringAttribute("name"));
                    }
                }
            }
            const auto evidence=exe.getSiblingFile(args=="--private-beta125-ui-test"?"PrivateBeta125Tests":args=="--starter133-legacy-ui-test"?"Starter133LegacyTests":"Starter133ExistingTests");
            uiTestHost=std::make_unique<RigHost>(uiTestRoot,false,true);tone3000TestAfterDelay(1000,[this,evidence]{finishPrivateBeta125(evidence,0);});return;
        }
        if(tokens.size()>0&&(tokens[0]=="--release134-ui-test"||tokens[0]=="--release135-ui-test"||tokens[0].startsWith("--startup135-")))
        {
            if(tokens.size()!=1&&tokens.size()!=3){setApplicationReturnValue(1);quit();return;}
            const auto exe=File::getSpecialLocation(File::currentExecutableFile);
            uiTestRoot=File::getSpecialLocation(File::tempDirectory).getChildFile("ToneGrid-release134-host-"+Uuid().toString());
            const auto folder=namamp::libraryForApplicationData(uiTestRoot);folder.createDirectory();
            for(int index=0;index<2;++index)
            {
                const auto destination=folder.getChildFile(index==0?"First.nam":"Second.nam");
                if(tokens.size()==3){if(!File(tokens[index+1].unquoted()).copyFileTo(destination)){setApplicationReturnValue(1);quit();return;}}
                else
                {
                    auto payload=nlohmann::json::parse(namTests::linear().toStdString());
                    payload["metadata"]={{"gear_type","amp_cab"},{"loudness",-26.757983107032274}};
                    if(index==0)payload["metadata"]["input_level_dbu"]=6.5;
                    destination.replaceWithText(String(payload.dump()));
                }
            }
            namamp::Entry entry;namamp::Library library(folder);
            if(library.read(folder.getChildFile("First.nam"),entry).failed()){setApplicationReturnValue(1);quit();return;}
            XmlElement preference("AmpSelection");preference.setAttribute("version",1);preference.setAttribute("type","NAM");
            preference.addChildElement(new XmlElement(namamp::encode({entry.identity,0,0})));
            if(tokens[0]=="--startup135-missing-nam")preference.getChildByName("AmpSource")->setAttribute("sha256",String::repeatedString("0",64));
            if(tokens[0]=="--startup135-missing-vst"||tokens[0]=="--startup135-interrupted")
            {
                preference.setAttribute("type","VST3");PluginDescription missing;missing.name="Not installed";missing.pluginFormatName="VST3";missing.manufacturerName="Synthetic";missing.uniqueId=135;missing.version="1";missing.fileOrIdentifier=uiTestRoot.getChildFile("Missing.vst3").getFullPathName();
                vst3::Catalog choice;choice.add(missing);vst3::CacheStore(uiTestRoot.getChildFile("ToneGrid/Vst3/selected.xml")).write(choice);
                if(tokens[0]=="--startup135-interrupted")rig::writeXml(*missing.createXml(),uiTestRoot.getChildFile("ToneGrid/Vst3/loading.xml"));
            }
            rig::writeXml(preference,uiTestRoot.getChildFile("ToneGrid/Amp/selected.xml"));
            if(tokens[0]=="--startup135-invalid")uiTestRoot.getChildFile("ToneGrid/Amp/selected.xml").replaceWithText("<AmpSelection version=\"1\" type=\"NAM\"><Broken/></AmpSelection>");
            uiTestHost=std::make_unique<RigHost>(uiTestRoot,false);
            if(tokens[0].startsWith("--startup135-"))uiTestHost->forceSafeStartupForTest();
            const auto output=exe.getSiblingFile(tokens[0].startsWith("--startup135-")?"Release135Tests-"+tokens[0].fromLastOccurrenceOf("-",false,false):tokens[0]=="--release135-ui-test"?"Release135Tests":tokens.size()==3?"Release134ActualTests":"Release134Tests");output.createDirectory();
            tone3000TestAfterDelay(1000,[this,output]{finishRelease134(output,0);});return;
        }
        if(args=="--learn132-ui-test"||args=="--release130-ui-test"||args=="--calibration117-ui-test"||args=="--calibration118-ui-test"||args=="--amp-eq121-ui-test"||args=="--release122-ui-test"||args=="--ui123-test"||args=="--nam-eq123-ui-test"||args=="--library124-ui-test"||args=="--normalization125-ui-test"||args=="--beta-welcome125-ui-test"||args=="--audio-recovery126-ui-test")
        {
            const auto exe=File::getSpecialLocation(File::currentExecutableFile);
            uiTestRoot=File::getSpecialLocation(File::tempDirectory).getChildFile("ToneGrid-calibration-host-"+Uuid().toString());
            const auto folder=namamp::libraryForApplicationData(uiTestRoot);folder.createDirectory();
            for(int index=0;index<2;++index)
            {
                auto payload=nlohmann::json::parse(namTests::linear().toStdString());
                payload["metadata"]={{"input_level_dbu",index==0?18:6},{"loudness",-18}};
                if(args=="--normalization125-ui-test"&&index==0){payload["metadata"]["loudness"]=-24;payload["metadata"]["Tonegrid_Normalization"]={{"version",1},{"trim_db",6}};}
                folder.getChildFile(index==0?"First.nam":"Second.nam").replaceWithText(String(payload.dump()));
            }
            namamp::Entry entry;namamp::Library library(folder);
            if(library.read(folder.getChildFile("First.nam"),entry).failed()){setApplicationReturnValue(1);quit();return;}
            XmlElement preference("AmpSelection");preference.setAttribute("version",1);preference.setAttribute("type","NAM");
            preference.addChildElement(new XmlElement(namamp::encode({entry.identity,0,0})));
            if(args=="--normalization125-ui-test"){auto settings=tonegrid::output::Settings{};settings.ampNormalizationEnabled=true;preference.addChildElement(new XmlElement(tonegrid::output::encode(settings)));}
            rig::writeXml(preference,uiTestRoot.getChildFile("ToneGrid/Amp/selected.xml"));
            uiTestHost=std::make_unique<RigHost>(uiTestRoot,false);
            // Never write test reports into the signed macOS application bundle.
#if JUCE_MAC
            const auto outputFolder=exe.getParentDirectory().getParentDirectory().getParentDirectory().getParentDirectory().getChildFile("Calibration117Tests");
#else
            const auto outputFolder=exe.getSiblingFile("Calibration117Tests");
#endif
            if(outputFolder.createDirectory().failed()){setApplicationReturnValue(1);quit();return;}
            if(args=="--learn132-ui-test"){tone3000TestAfterDelay(1000,[this,outputFolder]{finishLearn132(outputFolder,0);});return;}
            if(args=="--release130-ui-test"){tone3000TestAfterDelay(1000,[this,outputFolder]{finishRelease130(outputFolder,0);});return;}
            if(args=="--audio-recovery126-ui-test"){tone3000TestAfterDelay(1000,[this,outputFolder]{finishAudioRecovery126(outputFolder,0);});return;}
            if(args=="--beta-welcome125-ui-test"){tone3000TestAfterDelay(1000,[this,outputFolder]{finishBetaWelcome125(outputFolder,0);});return;}
            if(args=="--normalization125-ui-test"){tone3000TestAfterDelay(1000,[this,outputFolder]{finishNormalization125(outputFolder,{},true,false,0);});return;}
            if(args=="--ui123-test"){tone3000TestAfterDelay(1000,[this,outputFolder]{finishUi123(outputFolder,0);});return;}
            if(args=="--library124-ui-test"){tone3000TestAfterDelay(1000,[this,outputFolder]{finishLibrary124(outputFolder,{},true,false,0);});return;}
            if(args=="--nam-eq123-ui-test"){tone3000TestAfterDelay(1000,[this,outputFolder]{finishNamEq123(outputFolder,{},true,false,0);});return;}
            if(args=="--release122-ui-test"){tone3000TestAfterDelay(1000,[this,outputFolder]{finishRelease122(outputFolder,{},true,false,0);});return;}
            if(args=="--amp-eq121-ui-test"){tone3000TestAfterDelay(1000,[this,outputFolder]{finishAmpEq121(outputFolder,{},true,false,0);});return;}
            const auto reportFile=outputFolder.getChildFile("calibration-host-tests.txt");
            tone3000TestAfterDelay(1000,[this,reportFile]{finishCalibration117(reportFile,{},true,false,0);});return;
        }
        if(args=="--tone3000-connection-test")
        {
            String result;bool ok=true;int checks=0;const auto check=[&](bool pass,const String& message){++checks;ok=ok&&pass;result+=(pass?"PASS ":"FAIL ")+String("Tone3000 live ")+message+"\n";};
            using namespace tonegrid::t3k;Tone3000OAuthSession session;HttpTransport transport;std::atomic<bool> cancelled{false};
            const auto address=session.authorizationUrl();const auto accepted=transport.perform({address,"GET",{},{},64*1024,false},cancelled);
            check(accepted.status>=300&&accepted.status<400,"confirmed client reaches sign-in redirect, HTTP "+String(accepted.status));
            const auto wrong=transport.perform({address.replace("t3k_pub_zbn539j1yH1giZnQWE0VhLuc9izCIPxp","t3k_pub_zbnF39j1yH1giZnQWE0VhLuc9izCIPxp"),"GET",{},{},64*1024,false},cancelled);
            check(wrong.status==400&&JSON::parse(wrong.bytes.toString())["error"].toString()=="invalid_client","native HTTP transport preserves the invalid_client error body");
            check(authorizationFailure(wrong,{}).contains("public key"),"rejected client has a configuration message instead of a false network message");
            result+=String(checks)+" Tone3000 live connection checks; no sign-in or token exchange\n";
            const bool written=File::getSpecialLocation(File::currentExecutableFile).getSiblingFile("tone3000-connection-tests.txt").replaceWithText(result);setApplicationReturnValue(ok&&written?0:1);quit();return;
        }
        if(args=="--tone3000-ui-test"||args=="--tone3000-browser-preview"||args=="--ux112-preview")
        {
            const auto exe=File::getSpecialLocation(File::currentExecutableFile);uiTestRoot=File::getSpecialLocation(File::tempDirectory).getChildFile("ToneGrid-tone3000-ui-"+Uuid().toString());
            const auto folder=namamp::libraryForApplicationData(uiTestRoot);folder.createDirectory();const auto file=folder.getChildFile("Initial.nam");file.replaceWithText(namTests::linear());
            namamp::Entry entry;namamp::Library library(folder);if(library.read(file,entry).failed()){setApplicationReturnValue(1);quit();return;}
            namamp::State state{entry.identity,0,-8};XmlElement preference("AmpSelection");preference.setAttribute("version",1);preference.setAttribute("type","NAM");preference.addChildElement(new XmlElement(namamp::encode(state)));
            rig::writeXml(preference,uiTestRoot.getChildFile("ToneGrid/Amp/selected.xml"));uiTestHost=std::make_unique<RigHost>(uiTestRoot,false);
            if(args=="--ux112-preview")
            {
                class Preview final:public DocumentWindow{public:Preview():DocumentWindow(String("ToneGrid ")+JUCE_APPLICATION_VERSION_STRING+" - isolated preview",Colour(0xff14871c),allButtons){setUsingNativeTitleBar(true);}void closeButtonPressed()override{JUCEApplication::getInstance()->quit();}};
                uiPreviewWindow=std::make_unique<Preview>();uiPreviewWindow->setIcon(ImageCache::getFromMemory(ToneGridApplicationAssets::ToneGrid512_png,ToneGridApplicationAssets::ToneGrid512_pngSize));uiPreviewWindow->setContentNonOwned(uiTestHost.get(),true);uiPreviewWindow->centreWithSize(1660,940);uiPreviewWindow->setVisible(true);
                tone3000TestAfterDelay(15000,[this]{if(uiTestHost)uiTestHost->prepareUx112Preview();});tone3000TestAfterDelay(180000,[this]{quit();});return;
            }
            if(args=="--tone3000-browser-preview")
            {uiTestHost->prepareNamBrowserSnapshot();tone3000TestAfterDelay(1000,[this]{uiTestHost->openTone3000Preview();});tone3000TestAfterDelay(180000,[this]{quit();});return;}
            tone3000TestAfterDelay(15000,[this,exe]
            {
                String result;bool ok=uiTestHost->verifyTone3000Workflow(result);
                tone3000TestAfterDelay(200,[this,exe,result,ok]() mutable
                {
                    finishTone3000BrowserInitialisation(exe,result,ok,0);
                });
            });return;
        }
        if(tokens.size()==2&&tokens[0]=="--nam-integration-test")
        {
            const auto exe=File::getSpecialLocation(File::currentExecutableFile);String result;
            const bool ok=runNamAmpIntegrationTests(File(tokens[1].unquoted()),tonegrid::paths::installed().models(),result);
            const bool written=exe.getSiblingFile("nam-integration-tests.txt").replaceWithText(result);setApplicationReturnValue(ok&&written?0:1);quit();return;
        }
        if(tokens.size()==2&&(tokens[0]=="--tonebank-ui-test"||tokens[0]=="--tonebank-no-amp-ui-test"))
        {
            const auto exe=File::getSpecialLocation(File::currentExecutableFile);vst3::Catalog cache;const File original(tokens[1].unquoted());
            uiTestRoot=File::getSpecialLocation(File::tempDirectory).getChildFile("ToneGrid-tonebank-ui-test-"+Uuid().toString());
            const auto libraryFolder=namamp::libraryForApplicationData(uiTestRoot);const auto file=libraryFolder.getChildFile("Moved/Renamed-Mark.nam");
            if(file.getParentDirectory().createDirectory().failed()||!original.copyFileTo(file)||vst3::CacheStore(exe.getSiblingFile("vst3-test-cache.xml")).read(cache).failed()||!vst3::CacheStore(uiTestRoot.getChildFile("ToneGrid/Vst3/plugins.xml")).write(cache)){setApplicationReturnValue(1);quit();return;}
            namamp::Entry entry;namamp::Library library(libraryFolder);if(library.read(file,entry).failed()){setApplicationReturnValue(1);quit();return;}
            const bool bootstrap=tokens[0]=="--tonebank-no-amp-ui-test";
            namamp::State state{entry.identity,5,-8};if(bootstrap)state.model.file="not-installed.nam";XmlElement preference("AmpSelection");preference.setAttribute("version",1);preference.setAttribute("type","NAM");preference.addChildElement(new XmlElement(namamp::encode(state)));
            auto fx=tonegrid::output::Settings{};fx.ampNormalizationEnabled=false;fx.expandedEffects=true;preference.addChildElement(new XmlElement(tonegrid::output::encode(fx)));
            if(!rig::writeXml(preference,uiTestRoot.getChildFile("ToneGrid/Amp/selected.xml"))){setApplicationReturnValue(1);quit();return;}
            uiTestHost=std::make_unique<RigHost>(uiTestRoot,false);
            Timer::callAfterDelay(15000,[this,exe,bootstrap]
            {
                String result;const bool ok=uiTestHost->verifyToneBankWorkflow(result,bootstrap);uiTestHost->takeScreenshot(exe.getSiblingFile("tonebank-main.png"));uiTestHost.reset();
                const auto temp=File::getSpecialLocation(File::tempDirectory);const bool removed=uiTestRoot.isAChildOf(temp)&&uiTestRoot.deleteRecursively();result+=removed?"PASS ToneBank UI temporary storage removed\n":"FAIL ToneBank UI cleanup\n";
                const bool written=exe.getSiblingFile("tonebank-ui-tests.txt").replaceWithText(result);setApplicationReturnValue(ok&&removed&&written?0:1);quit();
            });return;
        }

        if(tokens.size()==2&&tokens[0]=="--nam-ui-test")
        {
            const auto exe=File::getSpecialLocation(File::currentExecutableFile);vst3::Catalog cache;const File original(tokens[1].unquoted());
            uiTestRoot=File::getSpecialLocation(File::tempDirectory).getChildFile("ToneGrid-nam-ui-test-"+Uuid().toString());
            const auto folder=namamp::libraryForApplicationData(uiTestRoot).getChildFile("JM Continuum");
            if(folder.createDirectory().failed()||!original.copyFileTo(folder.getChildFile(original.getFileName()))||vst3::CacheStore(exe.getSiblingFile("vst3-test-cache.xml")).read(cache).failed()||!vst3::CacheStore(uiTestRoot.getChildFile("ToneGrid/Vst3/plugins.xml")).write(cache)){setApplicationReturnValue(1);quit();return;}
            uiTestHost=std::make_unique<RigHost>(uiTestRoot,false);
            Timer::callAfterDelay(15000,[this,exe]
            {
                String result;const bool ok=uiTestHost->verifyNamWorkflow(result);uiTestHost->takeScreenshot(exe.getSiblingFile("nam-ui-small.png"));uiTestHost->setSize(1660,940);uiTestHost->takeScreenshot(exe.getSiblingFile("nam-ui.png"));
                uiTestHost->prepareNamBrowserSnapshot();
                // TreeView lays out row components asynchronously. Snapshot after
                // the real message loop has updated them, without a nested loop.
                Timer::callAfterDelay(200,[this,exe,result,ok]() mutable
                {
                    uiTestHost->takeNamBrowserScreenshot(exe.getSiblingFile("nam-tree-browser.png"));uiTestHost.reset();uiTestHost=std::make_unique<RigHost>(uiTestRoot,false);
                    Timer::callAfterDelay(15000,[this,exe,result,ok]() mutable
                    {
                        const bool restarted=uiTestHost->verifyNamRestart(result);uiTestHost.reset();const auto temp=File::getSpecialLocation(File::tempDirectory);
                        const bool removed=uiTestRoot.isAChildOf(temp)&&uiTestRoot.deleteRecursively();result+=removed?"PASS NAM UI temporary storage removed\n":"FAIL NAM UI cleanup\n";
                        const bool written=exe.getSiblingFile("nam-ui-tests.txt").replaceWithText(result);setApplicationReturnValue(ok&&restarted&&removed&&written?0:1);quit();
                    });
                });
            });return;
        }
        if(args=="--vst3-integration-test")
        {
            const auto exe=File::getSpecialLocation(File::currentExecutableFile);String report;
            const bool ok=runVst3IntegrationTests(exe,report);const bool written=exe.getSiblingFile("vst3-integration-tests.txt").replaceWithText(report);
            setApplicationReturnValue(ok&&written?0:1);quit();return;
        }
        if(args=="--vst3-ui-test")
        {
            const auto exe=File::getSpecialLocation(File::currentExecutableFile);vst3::Catalog cache;
            if(vst3::CacheStore(exe.getSiblingFile("vst3-test-cache.xml")).read(cache).failed()){setApplicationReturnValue(1);quit();return;}
            uiTestRoot=File::getSpecialLocation(File::tempDirectory).getChildFile("ToneGrid-ui-test-"+Uuid().toString());
            if(!vst3::CacheStore(uiTestRoot.getChildFile("ToneGrid/Vst3/plugins.xml")).write(cache)){setApplicationReturnValue(1);quit();return;}
            uiTestHost=std::make_unique<RigHost>(uiTestRoot,false);
            Timer::callAfterDelay(15000,[this,exe]
            {
                String result;const bool ok=uiTestHost->verifyVst3Workflow(result);
                uiTestHost->takeScreenshot(exe.getSiblingFile("vst3-ui-small.png"));uiTestHost->setSize(1660,940);uiTestHost->takeScreenshot(exe.getSiblingFile("vst3-ui.png"));
                uiTestHost.reset();const auto temp=File::getSpecialLocation(File::tempDirectory);
                const bool removed=uiTestRoot.isAChildOf(temp)&&uiTestRoot.deleteRecursively();result+=removed?"PASS UI synthetic storage removed\n":"FAIL UI synthetic storage cleanup\n";
                const bool written=exe.getSiblingFile("vst3-ui-tests.txt").replaceWithText(result);setApplicationReturnValue(ok&&removed&&written?0:1);quit();
            });return;
        }
        if(args=="--comp-preview")
        {
            auto folder=File::getSpecialLocation(File::currentExecutableFile).getParentDirectory();
            board::Processor processor;auto result=processor.load(tonegrid::paths::installed().models());
            if(result.failed()){setApplicationReturnValue(1);quit();return;}
            PedalCard card((int)board::definitionRowsById[(size_t)board::id(board::PedalId::StudioComp)],processor);card.setSize(240,650);card.midi({},false,true);
            Image preview(Image::ARGB,560,715,true);
            { Graphics g(preview);g.fillAll(Colour(0xff131b21));
            g.setFont(FontOptions("Segoe UI",15.f,Font::bold));g.setColour(Colours::white);
            g.drawText("COMP / OFF",20,8,240,30,Justification::centred);g.drawText("COMP / ON",300,8,240,30,Justification::centred);
            g.drawImageAt(card.createComponentSnapshot(card.getLocalBounds()),20,45);card.toggle();
            g.drawImageAt(card.createComponentSnapshot(card.getLocalBounds()),300,45);
            }
            auto output=folder.getChildFile("comp-design-preview.png").createOutputStream();
            if(output){output->setPosition(0);output->truncate();PNGImageFormat().writeImageToStream(preview,*output);}else setApplicationReturnValue(1);
            quit();return;
        }
        if(args=="--tonex-initialization-probe")
        {
            tonexProbe=std::make_unique<TonexInitializationProbe>([this](String result,bool ok){File::getSpecialLocation(File::currentExecutableFile).getSiblingFile("tonex-initialization-probe.txt").replaceWithText(result);setApplicationReturnValue(ok?0:1);quit();});return;
        }
        if(args=="--integration-test")
        {
            String results;auto exe=File::getSpecialLocation(File::currentExecutableFile);const bool ok=runIntegrationTests(tonegrid::paths::installed().models(),results);
            const bool written=exe.getSiblingFile("integration-tests.txt").replaceWithText(results);setApplicationReturnValue(ok&&written?0:1);quit();return;
        }
        if(args=="--self-test")
        {
            String results;auto exe=File::getSpecialLocation(File::currentExecutableFile);const bool ok=runBoardTests(tonegrid::paths::installed().models(),results);
            const bool written=exe.getSiblingFile("board-tests.txt").replaceWithText(results);setApplicationReturnValue(ok&&written?0:1);quit();return;
        }
        if(args=="--diagnose-audio")
        {
            AudioDeviceManager manager;String report;for(auto* type:manager.getAvailableDeviceTypes()){type->scanForDevices();report+=type->getTypeName()+": "+type->getDeviceNames().joinIntoString(", ")+"\n";}
            for(const auto& d:MidiInput::getAvailableDevices())report+="MIDI: "+d.name+"\n";
            File::getSpecialLocation(File::currentExecutableFile).getSiblingFile("audio-devices.txt").replaceWithText(report);quit();return;
        }
        if(args=="--window125-test")
        {
            uiTestRoot=File::getSpecialLocation(File::tempDirectory).getChildFile("ToneGrid-window125-"+Uuid().toString());
            const auto libraryFolder=namamp::libraryForApplicationData(uiTestRoot);libraryFolder.createDirectory();
            const auto file=libraryFolder.getChildFile("Window test.nam");file.replaceWithText(namTests::linear());
            namamp::Entry entry;namamp::Library library(libraryFolder);
            if(library.read(file,entry).failed()){setApplicationReturnValue(1);quit();return;}
            XmlElement preference("AmpSelection");preference.setAttribute("version",1);preference.setAttribute("type","NAM");
            preference.addChildElement(new XmlElement(namamp::encode({entry.identity,0,0})));
            rig::writeXml(preference,uiTestRoot.getChildFile("ToneGrid/Amp/selected.xml"));
            window=std::make_unique<MainWindow>(uiTestRoot,false);
            const auto folder=File::getSpecialLocation(File::currentExecutableFile).getParentDirectory().getChildFile("Window125Tests");
            tone3000TestAfterDelay(1000,[this,folder]{finishWindow125(folder,0);});return;
        }
        window=std::make_unique<MainWindow>();
        legacyProfileCleanup.start(tonegrid::paths::applicationDataRoot());
        if(args=="--workflow-test")Timer::callAfterDelay(15000,[this]{auto* host=&window->host();auto folder=File::getSpecialLocation(File::currentExecutableFile).getParentDirectory();const bool ok=host->verifyWorkflow(folder.getChildFile("workflow-tests.txt"));host->takeScreenshot(folder.getChildFile("tonegrid.png"));host->setSize(1360,820);host->takeScreenshot(folder.getChildFile("tonegrid-small.png"));setApplicationReturnValue(ok?0:1);quit();});
        if(args=="--ui-snapshot")Timer::callAfterDelay(15000,[this]{if(window){auto* host=&window->host();auto folder=File::getSpecialLocation(File::currentExecutableFile).getParentDirectory();host->takeScreenshot(folder.getChildFile("pedalboard.png"));host->setSize(1360,820);host->takeScreenshot(folder.getChildFile("pedalboard-small.png"));for(auto* child:host->getChildren())if(auto* card=dynamic_cast<PedalCard*>(child))if(auto* info=dynamic_cast<TextButton*>(card->findChildWithID("capture-info"))){info->onClick();host->takeScreenshot(folder.getChildFile("pedalboard-capture-info.png"));break;}}quit();});
    }
    void shutdown()override
    {
        legacyProfileCleanup.cancel();
        tone3000BrowserProbe.reset();recordVideoProbe.reset();learnBrowserProbe.reset();jamBrowserProbe.reset();window.reset();uiPreviewWindow.reset();uiTestHost.reset();tonexProbe.reset();
        const auto temp=File::getSpecialLocation(File::tempDirectory);if(!keepValidationProfile&&uiTestRoot.isAChildOf(temp))uiTestRoot.deleteRecursively();
    }
private:
    static void tone3000TestAfterDelay(int milliseconds,std::function<void()> action)
    {
        class Step final:private Timer,private DeletedAtShutdown
        {
        public:Step(int delay,std::function<void()> next):action(std::move(next)){startTimer(delay);}
        private:
            void timerCallback() override
            {
                // Native accessibility/WebView2 calls can pump messages while destroying
                // a window. Unregister before invoking the one-shot to prevent reentry.
                stopTimer();auto next=std::move(action);delete this;if(next)next();
            }
            std::function<void()> action;
        };
        new Step(milliseconds,std::move(action));
    }
    String record141Report;
    void finishRecord141(int attempt)
    {
        const auto folder=File::getSpecialLocation(File::currentExecutableFile).getSiblingFile("Record141-Validation");
        const int result=uiTestHost?uiTestHost->tickRecord141(folder,record141Report):-1;
        if(result==0&&attempt<1800){tone3000TestAfterDelay(100,[this,attempt]{finishRecord141(attempt+1);});return;}
        if(result==0)record141Report+="FAIL Record141 host workflow timeout\n";
        folder.createDirectory();folder.getChildFile("host-tests.txt").replaceWithText(record141Report);
        setApplicationReturnValue(result==1?0:1);quit();
    }
    void finishStartup143(int attempt)
    {
        const auto exe=File::getSpecialLocation(File::currentExecutableFile);
#if JUCE_MAC
        const auto evidence=exe.getParentDirectory().getParentDirectory().getParentDirectory().getParentDirectory().getChildFile("Startup143Tests");
#else
        const auto evidence=exe.getSiblingFile("Startup143Tests");
#endif
        const int status=window?window->host().stepStartup143(evidence):-1;
        if(status==0&&attempt<18000){tone3000TestAfterDelay(10,[this,attempt]{finishStartup143(attempt+1);});return;}
        if(status==0)evidence.getChildFile("startup143-tests.txt").replaceWithText("FAIL Startup workflow timeout");
        window.reset();setApplicationReturnValue(status==1?0:1);quit();
    }
    void finishFactoryRefinements142(int attempt)
    {
        const auto exe=File::getSpecialLocation(File::currentExecutableFile);
#if JUCE_MAC
        const auto evidence=exe.getParentDirectory().getParentDirectory().getParentDirectory().getParentDirectory().getParentDirectory().getChildFile("FactoryRefinements142Tests");
#else
        const auto evidence=exe.getSiblingFile("FactoryRefinements142Tests");
#endif
        evidence.createDirectory();const int status=uiTestHost?uiTestHost->stepFactoryRefinements142(evidence):-1;
        if(status==0&&attempt<1800){tone3000TestAfterDelay(100,[this,attempt]{finishFactoryRefinements142(attempt+1);});return;}
        if(status==0)evidence.getChildFile("native-refinements142.txt").replaceWithText("FAIL Native refinement test timeout\n"+(uiTestHost?uiTestHost->refinementProbe.report:String{}));
        uiTestHost.reset();setApplicationReturnValue(status==1?0:1);quit();
    }
    void finishIRPrototypeTest(int attempt)
    {
        if(uiTestHost&&!uiTestHost->cabinetTestReady()&&attempt<1800){tone3000TestAfterDelay(100,[this,attempt]{finishIRPrototypeTest(attempt+1);});return;}
        const auto evidence=File::getSpecialLocation(File::currentExecutableFile).getSiblingFile("IR-Validation");String report;
        const bool ok=uiTestHost&&uiTestHost->cabinetTestReady()&&uiTestHost->verifyIRPrototype(evidence,report);
        if(report.isEmpty())report="FAIL Prototype startup timeout\n";evidence.createDirectory();evidence.getChildFile("host-tests.txt").replaceWithText(report);
        setApplicationReturnValue(ok?0:1);quit();
    }
    void finishWindow125(const File& folder,int attempt)
    {
        if(window&&!window->host().calibration117Ready()&&attempt<100)
        {tone3000TestAfterDelay(100,[this,folder,attempt]{finishWindow125(folder,attempt+1);});return;}
        String report;const bool made=folder.createDirectory().wasOk();
        const bool ok=made&&window&&window->host().verifyWindow125(report,folder);window.reset();
        const bool removed=uiTestRoot.isAChildOf(File::getSpecialLocation(File::tempDirectory))&&uiTestRoot.deleteRecursively();
        const bool written=folder.getChildFile("window125-tests.txt").replaceWithText(report);setApplicationReturnValue(ok&&removed&&written?0:1);quit();
    }
    void finishLearn132(const File& folder,int attempt)
    {
        if(uiTestHost&&!uiTestHost->calibration117Ready()&&attempt<100){tone3000TestAfterDelay(100,[this,folder,attempt]{finishLearn132(folder,attempt+1);});return;}
        String report;const bool ok=uiTestHost&&uiTestHost->verifyLearn132(report,folder);uiTestHost.reset();
        const bool removed=uiTestRoot.isAChildOf(File::getSpecialLocation(File::tempDirectory))&&uiTestRoot.deleteRecursively();
        const bool written=folder.getChildFile("learn132-host-tests.txt").replaceWithText(report);setApplicationReturnValue(ok&&removed&&written?0:1);quit();
    }
    void finishRelease130(const File& folder,int attempt)
    {
        if(uiTestHost&&!uiTestHost->calibration117Ready()&&attempt<100){tone3000TestAfterDelay(100,[this,folder,attempt]{finishRelease130(folder,attempt+1);});return;}
        String report;const bool ok=uiTestHost&&uiTestHost->verifyRelease130(report,folder);uiTestHost.reset();
        const bool removed=uiTestRoot.isAChildOf(File::getSpecialLocation(File::tempDirectory))&&uiTestRoot.deleteRecursively();
        const bool written=folder.getChildFile("release130-host-tests.txt").replaceWithText(report);setApplicationReturnValue(ok&&removed&&written?0:1);quit();
    }
    void finishAudioRecovery126(const File& folder,int attempt)
    {
        if(uiTestHost&&!uiTestHost->calibration117Ready()&&attempt<100)
        {tone3000TestAfterDelay(100,[this,folder,attempt]{finishAudioRecovery126(folder,attempt+1);});return;}
        String report;const bool ok=uiTestHost&&uiTestHost->verifyAudioRecovery126(report,folder);uiTestHost.reset();
        const bool removed=uiTestRoot.isAChildOf(File::getSpecialLocation(File::tempDirectory))&&uiTestRoot.deleteRecursively();
        const bool written=folder.getChildFile("audio-recovery126-tests.txt").replaceWithText(report);setApplicationReturnValue(ok&&removed&&written?0:1);quit();
    }
    void finishLibrary124(const File& folder,String result,bool ok,bool restarted,int attempt)
    {
        if(uiTestHost&&!uiTestHost->calibration117Ready()&&attempt<100){tone3000TestAfterDelay(100,[this,folder,result,ok,restarted,attempt]{finishLibrary124(folder,result,ok,restarted,attempt+1);});return;}
        ok=(uiTestHost&&uiTestHost->verifyLibrary124(result,folder,restarted))&&ok;uiTestHost.reset();
        if(!restarted&&ok){uiTestHost=std::make_unique<RigHost>(uiTestRoot,false);tone3000TestAfterDelay(1000,[this,folder,result,ok]{finishLibrary124(folder,result,ok,true,0);});return;}
        const bool removed=uiTestRoot.isAChildOf(File::getSpecialLocation(File::tempDirectory))&&uiTestRoot.deleteRecursively();
        result+=removed?"PASS Library124 isolated profile removed\n":"FAIL Library124 cleanup\n";
        const bool written=folder.getChildFile("library124-host-tests.txt").replaceWithText(result);setApplicationReturnValue(ok&&removed&&written?0:1);quit();
    }
    void finishNamEq123(const File& folder,String result,bool ok,bool restarted,int attempt)
    {
        if(uiTestHost&&!uiTestHost->calibration117Ready()&&attempt<100){tone3000TestAfterDelay(100,[this,folder,result,ok,restarted,attempt]{finishNamEq123(folder,result,ok,restarted,attempt+1);});return;}
        ok=(uiTestHost&&uiTestHost->verifyNamEq123(result,folder,restarted))&&ok;uiTestHost.reset();
        if(!restarted&&ok){uiTestHost=std::make_unique<RigHost>(uiTestRoot,false);tone3000TestAfterDelay(1000,[this,folder,result,ok]{finishNamEq123(folder,result,ok,true,0);});return;}
        const bool removed=uiTestRoot.isAChildOf(File::getSpecialLocation(File::tempDirectory))&&uiTestRoot.deleteRecursively();
        result+=removed?"PASS NAM EQ123 isolated profile removed\n":"FAIL NAM EQ123 cleanup\n";
        const bool written=folder.getChildFile("nam-eq123-host-tests.txt").replaceWithText(result);setApplicationReturnValue(ok&&removed&&written?0:1);quit();
    }
    void finishUi123(const File& folder,int attempt)
    {
        if(uiTestHost&&!uiTestHost->calibration117Ready()&&attempt<100){tone3000TestAfterDelay(100,[this,folder,attempt]{finishUi123(folder,attempt+1);});return;}
        String result;const bool ok=uiTestHost&&uiTestHost->verifyUi123(result,folder);uiTestHost.reset();
        const bool removed=uiTestRoot.isAChildOf(File::getSpecialLocation(File::tempDirectory))&&uiTestRoot.deleteRecursively();
        result+=removed?"PASS UI123 isolated profile removed\n":"FAIL UI123 cleanup\n";
        const bool written=folder.getChildFile("ui123-host-tests.txt").replaceWithText(result);setApplicationReturnValue(ok&&removed&&written?0:1);quit();
    }
    void finishRelease122(const File& folder,String result,bool ok,bool restarted,int attempt)
    {
        if(uiTestHost&&!uiTestHost->calibration117Ready()&&attempt<100){tone3000TestAfterDelay(100,[this,folder,result,ok,restarted,attempt]{finishRelease122(folder,result,ok,restarted,attempt+1);});return;}
        ok=(uiTestHost&&uiTestHost->verifyRelease122(result,folder,restarted))&&ok;uiTestHost.reset();
        if(!restarted&&ok){uiTestHost=std::make_unique<RigHost>(uiTestRoot,false);tone3000TestAfterDelay(1000,[this,folder,result,ok]{finishRelease122(folder,result,ok,true,0);});return;}
        const bool removed=uiTestRoot.isAChildOf(File::getSpecialLocation(File::tempDirectory))&&uiTestRoot.deleteRecursively();
        result+=removed?"PASS 122 isolated profile removed\n":"FAIL 122 cleanup\n";
        const bool written=folder.getChildFile("release122-host-tests.txt").replaceWithText(result);setApplicationReturnValue(ok&&removed&&written?0:1);quit();
    }
    void finishAmpEq121(const File& folder,String result,bool ok,bool restarted,int attempt)
    {
        if(uiTestHost&&!uiTestHost->calibration117Ready()&&attempt<100){tone3000TestAfterDelay(100,[this,folder,result,ok,restarted,attempt]{finishAmpEq121(folder,result,ok,restarted,attempt+1);});return;}
        ok=(uiTestHost&&uiTestHost->verifyAmpEq121(result,folder,restarted))&&ok;uiTestHost.reset();
        if(!restarted&&ok){uiTestHost=std::make_unique<RigHost>(uiTestRoot,false);tone3000TestAfterDelay(1000,[this,folder,result,ok]{finishAmpEq121(folder,result,ok,true,0);});return;}
        const bool removed=uiTestRoot.isAChildOf(File::getSpecialLocation(File::tempDirectory))&&uiTestRoot.deleteRecursively();
        result+=removed?"PASS EQ121 isolated profile removed\n":"FAIL EQ121 cleanup\n";
        const bool written=folder.getChildFile("eq121-host-tests.txt").replaceWithText(result);setApplicationReturnValue(ok&&removed&&written?0:1);quit();
    }
    void finishRelease134(const File& folder,int attempt)
    {
        if(uiTestHost&&!uiTestHost->calibration117Ready()&&attempt<1200){tone3000TestAfterDelay(100,[this,folder,attempt]{finishRelease134(folder,attempt+1);});return;}
        String result;const bool ok=uiTestHost&&(folder.getFileName().startsWith("Release135Tests")?uiTestHost->verifyRelease135Ui(result,folder):uiTestHost->verifyRelease134Ui(result,folder));uiTestHost.reset();
        const bool removed=uiTestRoot.isAChildOf(File::getSpecialLocation(File::tempDirectory))&&uiTestRoot.deleteRecursively();
        const bool written=folder.getChildFile("release134-host-tests.txt").replaceWithText(result);setApplicationReturnValue(ok&&removed&&written?0:1);quit();
    }
    void finishPrivateBeta125(const File& folder,int attempt)
    {
        // First-run cabinet seeding now overlaps amp indexing instead of blocking
        // construction. Allow the complete cold factory scan, bounded at 120 s.
        if(uiTestHost&&!uiTestHost->calibration117Ready()&&attempt<1200){tone3000TestAfterDelay(100,[this,folder,attempt]{finishPrivateBeta125(folder,attempt+1);});return;}
        String report;const bool made=folder.createDirectory().wasOk();const bool ok=made&&uiTestHost&&uiTestHost->verifyPrivateBeta125(report,folder);uiTestHost.reset();
        const bool removed=uiTestRoot.isAChildOf(File::getSpecialLocation(File::tempDirectory))&&uiTestRoot.deleteRecursively();const bool written=folder.getChildFile("startup-tests.txt").replaceWithText(report);setApplicationReturnValue(ok&&removed&&written?0:1);quit();
    }
    void finishBetaWelcome125(const File& folder,int attempt)
    {
        if(uiTestHost&&!uiTestHost->calibration117Ready()&&attempt<100){tone3000TestAfterDelay(100,[this,folder,attempt]{finishBetaWelcome125(folder,attempt+1);});return;}
        String report;const bool ok=uiTestHost&&uiTestHost->verifyBetaWelcome125(report,folder);uiTestHost.reset();
        const bool removed=uiTestRoot.isAChildOf(File::getSpecialLocation(File::tempDirectory))&&uiTestRoot.deleteRecursively();
        const bool written=folder.getChildFile("beta-welcome125-host-tests.txt").replaceWithText(report);setApplicationReturnValue(ok&&removed&&written?0:1);quit();
    }
    void finishNormalization125(const File& folder,String report,bool ok,bool restarted,int attempt)
    {
        if(uiTestHost&&!uiTestHost->calibration117Ready()&&attempt<100){tone3000TestAfterDelay(100,[this,folder,report,ok,restarted,attempt]{finishNormalization125(folder,report,ok,restarted,attempt+1);});return;}
        ok=(uiTestHost&&uiTestHost->verifyNormalization125(report,folder,restarted))&&ok;uiTestHost.reset();
        if(!restarted&&ok){uiTestHost=std::make_unique<RigHost>(uiTestRoot,false);tone3000TestAfterDelay(1000,[this,folder,report,ok]{finishNormalization125(folder,report,ok,true,0);});return;}
        const bool removed=uiTestRoot.isAChildOf(File::getSpecialLocation(File::tempDirectory))&&uiTestRoot.deleteRecursively();
        const bool written=folder.getChildFile("normalization125-host-tests.txt").replaceWithText(report);setApplicationReturnValue(ok&&removed&&written?0:1);quit();
    }
    void finishCalibration117(const File& reportFile,String result,bool ok,bool restarted,int attempt)
    {
        if(uiTestHost&&!uiTestHost->calibration117Ready()&&attempt<100)
        {tone3000TestAfterDelay(100,[this,reportFile,result,ok,restarted,attempt]{finishCalibration117(reportFile,result,ok,restarted,attempt+1);});return;}
        const bool verified=uiTestHost&&uiTestHost->verifyCalibration117(result,reportFile.getParentDirectory(),restarted);ok=verified&&ok;
        uiTestHost.reset();
        if(!restarted&&ok)
        {uiTestHost=std::make_unique<RigHost>(uiTestRoot,false);tone3000TestAfterDelay(1000,[this,reportFile,result,ok]{finishCalibration117(reportFile,result,ok,true,0);});return;}
        const bool removed=uiTestRoot.isAChildOf(File::getSpecialLocation(File::tempDirectory))&&uiTestRoot.deleteRecursively();
        result+=removed?"PASS isolated calibration profile removed\n":"FAIL calibration test cleanup\n";
        const bool written=reportFile.replaceWithText(result);
        setApplicationReturnValue(ok&&removed&&written?0:1);quit();
    }
    void finishTone3000BrowserInitialisation(const File& exe,String result,bool ok,int attempt)
    {
        if(!uiTestHost)return;
        if(!uiTestHost->tone3000NativeBrowserReady()&&attempt<50){tone3000TestAfterDelay(100,[this,exe,result,ok,attempt]{finishTone3000BrowserInitialisation(exe,result,ok,attempt+1);});return;}
        const bool viewportOk=uiTestHost->verifyImportedViewport(result);
        const bool ready=uiTestHost->tone3000NativeBrowserReady();result+=ready?"PASS Tone3000 host native browser initialization completed\n":"FAIL Tone3000 host native browser initialization\n";
        ok=uiTestHost->verifyRelease113Workflow(result)&&ok;
        ok=uiTestHost->verifyRecording114Workflow(result)&&ok;
        ok=uiTestHost->verifyPopup115Workflow(result)&&ok;
        finishPopup115Prompts(exe,result,ok&&ready&&viewportOk,0);
    }
    void finishPopup115Prompts(const File& exe,String result,bool ok,int step,Component::SafePointer<Component> previous={},int attempt=0)
    {
        if(!uiTestHost)return;
        // Wait for JUCE's actual modal callback/destruction, without pumping a nested
        // message loop or assuming a fixed delay is long enough on every machine.
        if(previous&&attempt<50){tone3000TestAfterDelay(100,[this,exe,result,ok,step,previous,attempt]{finishPopup115Prompts(exe,result,ok,step,previous,attempt+1);});return;}
        if(previous){result+="FAIL 1.1.15 host modal callback timeout\n";ok=false;}
        Component::SafePointer<Component> dismissed;
        ok=uiTestHost->verifyPopup115PromptStep(step,result,dismissed)&&ok;
        if(step<5){tone3000TestAfterDelay(100,[this,exe,result,ok,step,dismissed]{finishPopup115Prompts(exe,result,ok,step+1,dismissed);});return;}
        uiTestHost->takeNamBrowserScreenshot(exe.getSiblingFile("tone3000-library.png"));uiTestHost->takeScreenshot(exe.getSiblingFile("tone3000-main.png"));uiTestHost.reset();
        finishTone3000UiTest(exe,result,ok,0);
    }
    void finishTone3000UiTest(const File& exe,String result,bool ok,int attempt)
    {
        // WebView2 releases its profile files asynchronously after its UI is destroyed.
        // Retry only this test's explicitly owned temporary directory, without blocking UI.
        const bool owned=uiTestRoot.isAChildOf(File::getSpecialLocation(File::tempDirectory))&&uiTestRoot.getFileName().startsWith("ToneGrid-tone3000-ui-");
        const bool removed=owned&&(!uiTestRoot.exists()||uiTestRoot.deleteRecursively());
        if(owned&&!removed&&attempt<20){tone3000TestAfterDelay(250,[this,exe,result,ok,attempt]{finishTone3000UiTest(exe,result,ok,attempt+1);});return;}
        result+=removed?"PASS Tone3000 host temporary storage removed\n":"FAIL Tone3000 host temporary storage cleanup\n";
        const bool written=exe.getSiblingFile("tone3000-ui-tests.txt").replaceWithText(result);setApplicationReturnValue(ok&&removed&&written?0:1);quit();
    }
    tonegrid::paths::LegacyProfileCleanup legacyProfileCleanup;
    std::unique_ptr<Tone3000NativeBrowserProbe> tone3000BrowserProbe;std::unique_ptr<RecordVideoBrowserProbe> recordVideoProbe;
    std::unique_ptr<Learn132BrowserProbe> learnBrowserProbe;std::unique_ptr<Jam131BrowserProbe> jamBrowserProbe;std::unique_ptr<TonexInitializationProbe> tonexProbe;std::unique_ptr<MainWindow> window;std::unique_ptr<DocumentWindow> uiPreviewWindow;std::unique_ptr<RigHost> uiTestHost;File uiTestRoot;bool keepValidationProfile=false;
};
START_JUCE_APPLICATION(Application)
