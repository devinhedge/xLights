#include <wx/xml/xml.h>

#include "ControllerEthernet.h"
#include "OutputManager.h"
#include "Output.h"
#include "../UtilFunctions.h"
#include "../SpecialOptions.h"
#include "../OutputModelManager.h"
#include "IPOutput.h"
#include "E131Output.h"
#include "ArtNetOutput.h"
#include "ZCPPOutput.h"
#include "DDPOutput.h"
#include "xxxEthernetOutput.h"
#include "../controllers/ControllerCaps.h"

wxPGChoices ControllerEthernet::__types;

void ControllerEthernet::InitialiseTypes(bool forceXXX)
{
    if (__types.GetCount() == 0)
    {
        __types.Add(OUTPUT_E131);
        __types.Add(OUTPUT_ZCPP);
        __types.Add(OUTPUT_ARTNET);
        __types.Add(OUTPUT_DDP);
        if (SpecialOptions::GetOption("xxx") == "true" || forceXXX)
        {
            __types.Add(OUTPUT_xxxETHERNET);
        }
    }
    else if (forceXXX)
    {
        bool found = false;
        for (size_t i = 0; i < __types.GetCount(); i++)
        {
            if (__types.GetLabel(i) == OUTPUT_xxxETHERNET)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            __types.Add(OUTPUT_xxxETHERNET);
        }
    }
}

inline std::string ControllerEthernet::GetFPPProxy() const { if (_fppProxy != "") return _fppProxy; return _outputManager->GetGlobalFPPProxy(); }

std::string ControllerEthernet::GetChannelMapping(int32_t ch) const
{
    wxString res = wxString::Format("Channel %ld maps to ...\nType: %s\nName: %s\nIP: %s\n", ch, GetProtocol(), GetName(), GetIP());

    int32_t sc;
    auto o = GetOutput(ch, sc);

    if (o->GetType() == OUTPUT_ARTNET || o->GetType() == OUTPUT_E131)
    {
        res += wxString::Format("Universe: %s\nChannel: %ld\n", o->GetUniverseString(), sc);
    }
    else if (o->GetType() == OUTPUT_xxxETHERNET)
    {
        auto xo = dynamic_cast<xxxEthernetOutput*>(o);
        res += wxString::Format("Port: %d\nChannel: %ld\n", xo->GetPort(), sc);
    }
    else
    {
        res += wxString::Format("Channel: %ld\n", sc);
    }

    if (!IsActive())
    {
        res += " INACTIVE\n";
    }

    return res;
}

std::string ControllerEthernet::GetColumn3Label() const
{
    if (_type == OUTPUT_E131 || _type == OUTPUT_ARTNET || _type == OUTPUT_xxxETHERNET)
    {
        if (_outputs.size() == 1)
        {
            return _outputs.front()->GetUniverseString();
        }
        else if (_outputs.size() > 1)
        {
            return _outputs.front()->GetUniverseString() + "-" + _outputs.back()->GetUniverseString();
        }
    }
    return wxString::Format("%d", GetId());
}

void ControllerEthernet::SetTransientData(int& cn, int& on, int32_t& startChannel, int& nullnumber)
{
    // copy down properties that should be on every output
    for (auto& it : _outputs)
    {
        if (it->GetType() == OUTPUT_E131)
        {
            dynamic_cast<E131Output*>(it)->SetPriority(_priority);
        }
        else if (it->GetType() == OUTPUT_ZCPP)
        {
            dynamic_cast<ZCPPOutput*>(it)->SetPriority(_priority);
        }
    }

    Controller::SetTransientData(cn, on, startChannel, nullnumber);
}

wxXmlNode* ControllerEthernet::Save()
{
    wxXmlNode* um = Controller::Save();

    um->AddAttribute("IP", _ip);
    um->AddAttribute("Protocol", _type);
    um->AddAttribute("FPPProxy", _fppProxy);
    um->AddAttribute("Priority", wxString::Format("%d", _priority));

    return um;
}

bool ControllerEthernet::SupportsUpload() const
{
    if (_type == OUTPUT_ZCPP) return false;

    auto c = ControllerCaps::GetControllerConfig(_vendor, _model, _firmwareVersion);

    if (c != nullptr)
    {
        return c->SupportsUpload();
    }

    return false;
}

Output::PINGSTATE ControllerEthernet::Ping()
{
    if (GetResolvedIP() == "MULTICAST")
    {
        _lastPingResult = Output::PINGSTATE::PING_UNAVAILABLE;
    }
    else
    {
        _lastPingResult = dynamic_cast<IPOutput*>(_outputs.front())->Ping(GetResolvedIP(), GetFPPProxy());
    }
	return GetLastPingState();
}

void ControllerEthernet::AsyncPing()
{
    // if one is already running dont start another
    if (_asyncPing.valid() && _asyncPing.wait_for(std::chrono::microseconds(1)) == std::future_status::timeout) return;

    _asyncPing = std::async(std::launch::async, &ControllerEthernet::Ping, this);
}

void ControllerEthernet::SetPriority(int priority)
{
    if (_priority != priority)
    {
        _priority = priority;
        _dirty = true;

        for (auto& it : _outputs)
        {
            if (dynamic_cast<E131Output*>(it) != nullptr)
            {
                dynamic_cast<E131Output*>(it)->SetPriority(priority);
            }
            else if (dynamic_cast<ZCPPOutput*>(it) != nullptr)
            {
                dynamic_cast<ZCPPOutput*>(it)->SetPriority(priority);
            }
        }
    }
}
\
bool ControllerEthernet::AllSameSize() const
{
    int32_t size = -1;
    for (const auto& it : _outputs)
    {
        if (size < 0)
        {
            size = it->GetChannels();
        }
        else if (it->GetChannels() != size)
        {
            return false;
        }
    }

    return true;
}

void ControllerEthernet::AddProperties(wxPropertyGrid* propertyGrid)
{
    wxPGProperty* p = nullptr;

    Controller::AddProperties(propertyGrid);

    if (_type == OUTPUT_E131)
    {
        p = propertyGrid->Append(new wxBoolProperty("Multicast", "Multicast", _ip == "MULTICAST"));
        p->SetEditor("CheckBox");
    }

    if (_ip != "MULTICAST")
    {
        p = propertyGrid->Append(new wxStringProperty("IP Address", "IP", _ip));
        p->SetHelpString("This must be unique across all controllers.");
    }

    p = propertyGrid->Append(new wxEnumProperty("Protocol", "Protocol", __types, EncodeChoices(__types, _type)));

    if (_type == OUTPUT_ZCPP)
    {
        p = propertyGrid->Append(new wxStringProperty("Multicast Address", "MulticastAddressDisplay", ZCPP_GetDataMulticastAddress(_ip)));
        p->ChangeFlag(wxPG_PROP_READONLY, true);
    }

    bool allSameSize = AllSameSize();
    if (_outputs.size() > 0)
    {
        _outputs.front()->AddProperties(propertyGrid, allSameSize);
    }

    if (_type == OUTPUT_E131 || _type == OUTPUT_ZCPP)
    {
        p = propertyGrid->Append(new wxUIntProperty("Priority", "Priority", _priority));
        p->SetAttribute("Min", 0);
        p->SetAttribute("Max", 100);
        p->SetEditor("SpinCtrl");
    }

    if (_type == OUTPUT_E131 || _type == OUTPUT_ARTNET || _type == OUTPUT_xxxETHERNET)
    {
        p = propertyGrid->Append(new wxBoolProperty("Managed", "Managed", _managed));
        p->SetEditor("CheckBox");

        if (_outputManager->GetControllerCount(GetType(), _ip) == 1)
        {
            // leave it editable
        }
        else
        {
            if (!_managed)
            {
                // we cant make it managed as duplicates exist
                p->ChangeFlag(wxPG_PROP_READONLY, true);
                p->SetHelpString("This controller cannot be made managed until all other controllers with the same IP address are removed.");
            }
            else
            {
                // This cant happen
                wxASSERT(false);
            }
        }
    }

    if (IsFPPProxyable())
    {
        p = propertyGrid->Append(new wxStringProperty("FPP Proxy IP/Hostname", "FPPProxy", GetControllerFPPProxy()));
        p->SetHelpString("This is typically the WIFI IP of a FPP instance that bridges two networks.");
    }

    if (_type == OUTPUT_DDP)
    {
        auto ddp = dynamic_cast<DDPOutput*>(_outputs.front());

        if (ddp != nullptr)
        {
            p = propertyGrid->Append(new wxUIntProperty("Channels Per Packet", "ChannelsPerPacket", ddp->GetChannelsPerPacket()));
            p->SetAttribute("Min", 1);
            p->SetAttribute("Max", 1440);
            p->SetEditor("SpinCtrl");

            p = propertyGrid->Append(new wxBoolProperty("Keep Channel Numbers", "KeepChannelNumbers", ddp->IsKeepChannelNumbers()));
            p->SetEditor("CheckBox");
        }
    }

    if (_type == OUTPUT_ZCPP)
    {
        auto zcpp = dynamic_cast<ZCPPOutput*>(_outputs.front());

        if (zcpp != nullptr)
        {
            p = propertyGrid->Append(new wxBoolProperty("Supports Virtual Strings", "SupportsVirtualStrings", zcpp->IsSupportsVirtualStrings()));
            p->SetEditor("CheckBox");

            p = propertyGrid->Append(new wxBoolProperty("Supports Smart Remotes", "SupportsSmartRemotes", zcpp->IsSupportsSmartRemotes()));
            p->SetEditor("CheckBox");

            p = propertyGrid->Append(new wxBoolProperty("Send Data Multicast", "SendDataMulticast", zcpp->IsMulticast()));
            p->SetEditor("CheckBox");

            p = propertyGrid->Append(new wxBoolProperty("Suppress Sending Configuration", "DontSendConfig", zcpp->IsDontConfigure()));
            p->SetEditor("CheckBox");
        }
    }

    if (_type == OUTPUT_E131 || _type == OUTPUT_ARTNET || _type == OUTPUT_xxxETHERNET)
    {
        auto u = "Start Universe";
        auto uc = "Universe Count";
        auto ud = "Universes";
        if (_type == OUTPUT_xxxETHERNET)
        {
            u = "Start Port";
            uc = "Port Count";
            ud = "Ports";
        }
        p = propertyGrid->Append(new wxUIntProperty(u, "Universe", _outputs.front()->GetUniverse()));
        p->SetAttribute("Min", 1);
        p->SetAttribute("Max", 64000);
        p->SetEditor("SpinCtrl");

        p = propertyGrid->Append(new wxUIntProperty(uc, "Universes", _outputs.size()));
        p->SetAttribute("Min", 1);
        p->SetAttribute("Max", 1000);
        p->SetEditor("SpinCtrl");

        if (_outputs.size() > 1)
        {
            p = propertyGrid->Append(new wxStringProperty(ud, "UniversesDisplay", _outputs.front()->GetUniverseString() + "- " + _outputs.back()->GetUniverseString()));
            p->ChangeFlag(wxPG_PROP_READONLY, true);
        }

        p = propertyGrid->Append(new wxBoolProperty("Individual Sizes", "IndivSizes", !allSameSize || _forceSizes));
        p->SetEditor("CheckBox");

        if (!allSameSize || _forceSizes)
        {
            wxPGProperty* p2 = propertyGrid->Append(new wxPropertyCategory("Sizes", "Sizes"));
            for (const auto& it : _outputs)
            {
                p = propertyGrid->AppendIn(p2, new wxUIntProperty(it->GetUniverseString(), "Channels/" + it->GetUniverseString(), it->GetChannels()));
                p->SetAttribute("Min", 1);
                p->SetAttribute("Max", it->GetMaxChannels());
                p->SetEditor("SpinCtrl");
            }
            p2->SetExpanded(true);
        }
        else
        {
            p = propertyGrid->Append(new wxUIntProperty("Channels", "Channels", _outputs.front()->GetChannels()));
            p->SetAttribute("Min", 1);
            p->SetAttribute("Max", _outputs.front()->GetMaxChannels());
            p->SetEditor("SpinCtrl");
        }
    }
    else
    {
        p = propertyGrid->Append(new wxUIntProperty("Channels", "Channels", _outputs.front()->GetChannels()));
        p->SetAttribute("Min", 1);
        p->SetAttribute("Max", _outputs.front()->GetMaxChannels());

        if (IsAutoSize())
        {
            p->ChangeFlag(wxPG_PROP_READONLY, true);
            p->SetHelpString("Channels cannot be changed when an output is set to Auto Size.");
        }
        else
        {
            p->SetEditor("SpinCtrl");
        }
    }
}

bool ControllerEthernet::HandlePropertyEvent(wxPropertyGridEvent& event, OutputModelManager* outputModelManager)
{
    if (Controller::HandlePropertyEvent(event, outputModelManager)) return true;

    wxString name = event.GetPropertyName();
    wxPropertyGrid* grid = dynamic_cast<wxPropertyGrid*>(event.GetEventObject());

    if (name == "IP")
    {
        SetIP(event.GetValue().GetString());
        outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANGE, "ControllerEthernet::HandlePropertyEvent::IP");
        outputModelManager->AddASAPWork(OutputModelManager::WORK_UPDATE_NETWORK_LIST, "ControllerEthernet::HandlePropertyEvent::IP", nullptr);
        outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANNELSCHANGE, "ControllerEthernet::HandlePropertyEvent::IP", nullptr);
        outputModelManager->AddLayoutTabWork(OutputModelManager::WORK_CALCULATE_START_CHANNELS, "ControllerEthernet::HandlePropertyEvent::IP", nullptr);        
        return true;
    }
    else if (name == "Multicast")
    {
        if (event.GetValue().GetBool())
        {
            SetIP("MULTICAST");
        }
        else
        {
            SetIP("");
        }
        outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANGE, "ControllerEthernet::HandlePropertyEvent::Multicast");
        outputModelManager->AddASAPWork(OutputModelManager::WORK_UPDATE_NETWORK_LIST, "ControllerEthernet::HandlePropertyEvent::Multicast", nullptr);
        return true;
    }
    else if (name == "Priority")
    {
        SetPriority(_priority = event.GetValue().GetLong());
        outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANGE, "ControllerEthernet::HandlePropertyEvent::Priority");
        return true;
    }
    else if (name == "FPPProxy")
    {
        SetFPPProxy(event.GetValue().GetString());
        outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANGE, "ControllerEthernet::HandlePropertyEvent::FPPProxy");
        return true;
    }
    else if (name == "ChannelsPerPacket")
    {
        dynamic_cast<DDPOutput*>(_outputs.front())->SetChannelsPerPacket(event.GetValue().GetLong());
        outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANGE, "ControllerEthernet::HandlePropertyEvent::ChannelsPerPacket");
        return true;
    }
    else if (name == "KeepChannelNumbers")
    {
        dynamic_cast<DDPOutput*>(_outputs.front())->SetKeepChannelNumber(event.GetValue().GetBool());
        outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANGE, "ControllerEthernet::HandlePropertyEvent::KeepChannelNumbers");
        return true;
    }
    else if (name == "SupportsVirtualStrings")
    {
        dynamic_cast<ZCPPOutput*>(_outputs.front())->SetSupportsVirtualStrings(event.GetValue().GetBool());
        outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANGE, "ControllerEthernet::HandlePropertyEvent::SupportsVirtualStrings");
        return true;
    }
    else if (name == "SupportsSmartRemotes")
    {
        dynamic_cast<ZCPPOutput*>(_outputs.front())->SetSupportsSmartRemotes(event.GetValue().GetBool());
        outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANGE, "ControllerEthernet::HandlePropertyEvent::SupportsSmartRemotes");
        return true;
    }
    else if (name == "SendDataMulticast")
    {
        dynamic_cast<ZCPPOutput*>(_outputs.front())->SetMulticast(event.GetValue().GetBool());
        outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANGE, "ControllerEthernet::HandlePropertyEvent::SendDataMulticast");
        return true;
    }
    else if (name == "DontSendConfig")
    {
        dynamic_cast<ZCPPOutput*>(_outputs.front())->SetDontConfigure(event.GetValue().GetBool());
        outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANGE, "ControllerEthernet::HandlePropertyEvent::DontSendConfig");
        return true;
    }
    else if (name == "Managed")
    {
        SetManaged(event.GetValue().GetBool());
        outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANGE, "ControllerEthernet::HandlePropertyEvent::Managed");
        outputModelManager->AddASAPWork(OutputModelManager::WORK_UPDATE_NETWORK_LIST, "ControllerEthernet::HandlePropertyEvent::Managed", nullptr);
        return true;
    }
    else if (name == "Protocol")
    {
        SetProtocol(Controller::DecodeChoices(__types, event.GetValue().GetLong()));

        outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANGE, "ControllerEthernet::HandlePropertyEvent::Type");
        outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANNELSCHANGE, "ControllerEthernet::HandlePropertyEvent::Type", nullptr);
        outputModelManager->AddASAPWork(OutputModelManager::WORK_UPDATE_NETWORK_LIST, "ControllerEthernet::HandlePropertyEvent::Type", nullptr);
        outputModelManager->AddLayoutTabWork(OutputModelManager::WORK_CALCULATE_START_CHANNELS, "ControllerEthernet::HandlePropertyEvent::Type", nullptr);
        return true;
    }
    else if (name == "Universe")
    {
        int univ = event.GetValue().GetLong();
        for (auto& it : _outputs)
        {
            it->SetUniverse(univ++);
        }
        outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANGE, "ControllerEthernet::HandlePropertyEvent::Universe");
        outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANNELSCHANGE, "ControllerEthernet::HandlePropertyEvent::Universe", nullptr);
        outputModelManager->AddASAPWork(OutputModelManager::WORK_UPDATE_NETWORK_LIST, "ControllerEthernet::HandlePropertyEvent::Universe", nullptr);
        outputModelManager->AddLayoutTabWork(OutputModelManager::WORK_CALCULATE_START_CHANNELS, "ControllerEthernet::HandlePropertyEvent::Universe", nullptr);
        return true;
    }
    else if (name == "Universes")
    {
        // add universes
        while (_outputs.size() < event.GetValue().GetLong())
        {
            if (_type == OUTPUT_E131)
            {
                _outputs.push_back(new E131Output());
            }
            else if (_type == OUTPUT_ARTNET)
            {
                _outputs.push_back(new ArtNetOutput());
            }
            else if (_type == OUTPUT_xxxETHERNET)
            {
                _outputs.push_back(new xxxEthernetOutput());
            }
            else
            {
                wxASSERT(false);
            }
            _outputs.back()->SetIP(_outputs.front()->GetIP());
            _outputs.back()->SetChannels(_outputs.front()->GetChannels());
            _outputs.back()->SetFPPProxyIP(_outputs.front()->GetFPPProxyIP());
            _outputs.back()->SetSuppressDuplicateFrames(_outputs.front()->IsSuppressDuplicateFrames());
            _outputs.back()->SetUniverse(_outputs.front()->GetUniverse() + _outputs.size() - 1);
        }

        // drop universes
        while (_outputs.size() > event.GetValue().GetLong())
        {
            delete _outputs.back();
            _outputs.pop_back();
        }

        outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANGE, "ControllerEthernet::HandlePropertyEvent::Universes");
        outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANNELSCHANGE, "ControllerEthernet::HandlePropertyEvent::Universes", nullptr);
        outputModelManager->AddASAPWork(OutputModelManager::WORK_UPDATE_NETWORK_LIST, "ControllerEthernet::HandlePropertyEvent::Universes", nullptr);
        outputModelManager->AddLayoutTabWork(OutputModelManager::WORK_CALCULATE_START_CHANNELS, "ControllerEthernet::HandlePropertyEvent::Universes", nullptr);
        return true;
    }
    else if (name == "IndivSizes")
    {
        _forceSizes = event.GetValue().GetBool();

        if (!_forceSizes)
        {
            for (auto& it : _outputs)
            {
                it->SetChannels(_outputs.front()->GetChannels());
            }
        }

        outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANGE, "ControllerEthernet::HandlePropertyEvent::IndivSizes");
        outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANNELSCHANGE, "ControllerEthernet::HandlePropertyEvent::IndivSizes", nullptr);
        outputModelManager->AddASAPWork(OutputModelManager::WORK_UPDATE_NETWORK_LIST, "ControllerEthernet::HandlePropertyEvent::IndivSizes", nullptr);
        outputModelManager->AddLayoutTabWork(OutputModelManager::WORK_CALCULATE_START_CHANNELS, "ControllerEthernet::HandlePropertyEvent::IndivSizes", nullptr);
        return true;
    }
    else if (name == "Channels")
    {
        for (auto& it : _outputs)
        {
            it->SetChannels(event.GetValue().GetLong());
            outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANGE, "ControllerEthernet::HandlePropertyEvent::Channels");
            outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANNELSCHANGE, "ControllerEthernet::HandlePropertyEvent::Channels", nullptr);
            outputModelManager->AddASAPWork(OutputModelManager::WORK_UPDATE_NETWORK_LIST, "ControllerEthernet::HandlePropertyEvent::Channels", nullptr);
            outputModelManager->AddLayoutTabWork(OutputModelManager::WORK_CALCULATE_START_CHANNELS, "ControllerEthernet::HandlePropertyEvent::Channels", nullptr);
        }
        return true;
    }
    else if (StartsWith(name, "Channels/"))
    {
        // the property label is the universe number so we look up the output based on that
        wxString n = event.GetProperty()->GetLabel();
        if (n.Contains("or")) n = n.AfterLast(' ');
        int univ = wxAtoi(n);
        for (auto& it : _outputs)
        {
            if (it->GetUniverse() == univ)
            {
                it->SetChannels(event.GetValue().GetLong());
                outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANGE, "ControllerEthernet::HandlePropertyEvent::Channels/");
                outputModelManager->AddASAPWork(OutputModelManager::WORK_NETWORK_CHANNELSCHANGE, "ControllerEthernet::HandlePropertyEvent::Channels/", nullptr);
                outputModelManager->AddASAPWork(OutputModelManager::WORK_UPDATE_NETWORK_LIST, "ControllerEthernet::HandlePropertyEvent::Channels/", nullptr);
                outputModelManager->AddLayoutTabWork(OutputModelManager::WORK_CALCULATE_START_CHANNELS, "ControllerEthernet::HandlePropertyEvent::Channels/", nullptr);
                return true;
            }
        }
    }

    if (_outputs.size() > 0)
    {
        if (_outputs.front()->HandlePropertyEvent(event, outputModelManager)) return true;
    }

    return false;
}

void ControllerEthernet::ValidateProperties(OutputManager* om, wxPropertyGrid* propGrid) const
{
    Controller::ValidateProperties(om, propGrid);

    auto p = propGrid->GetPropertyByName("Protocol");
    auto caps = ControllerCaps::GetControllerConfig(this);
    if (caps != nullptr && p != nullptr)
    {
        // controller must support the protocol
        if (!caps->IsValidInputProtocol(Lower(_type)))
        {
            p->SetBackgroundColour(*wxRED);
        }
        else
        {
            p->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX));
        }
    }

    p = propGrid->GetPropertyByName("Universes");
    if (caps != nullptr && p != nullptr && (_type == OUTPUT_E131 || _type == OUTPUT_ARTNET))
    {
        if (_outputs.size() > caps->GetMaxInputE131Universes())
        {
            p->SetBackgroundColour(*wxRED);
        }
        else
        {
            p->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX));
        }
    }

    p = propGrid->GetPropertyByName("Channels");
    if (caps != nullptr && p != nullptr && (_type == OUTPUT_E131 || _type == OUTPUT_ARTNET))
    {
        if (_outputs.front()->GetChannels() > caps->GetMaxInputUniverseChannels())
        {
            p->SetBackgroundColour(*wxRED);
        }
        else
        {
            p->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX));
        }
    }

    if (caps != nullptr && p != nullptr && (_type == OUTPUT_E131 || _type == OUTPUT_ARTNET))
    {
        for (const auto& it : _outputs)
        {
            p = propGrid->GetPropertyByName("Channels/" + it->GetUniverseString());
            if (p != nullptr)
            {
                if (it->GetChannels() > caps->GetMaxInputUniverseChannels())
                {
                    p->SetBackgroundColour(*wxRED);
                }
                else
                {
                    p->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX));
                }
            }
        }
    }
}

ControllerEthernet::ControllerEthernet(OutputManager* om, wxXmlNode* node, const std::string& showDir) : Controller(om, node, showDir)
{
    _type = node->GetAttribute("Protocol");
    InitialiseTypes(_type == OUTPUT_xxxETHERNET);
    _ip = node->GetAttribute("IP");
    _fppProxy = node->GetAttribute("FPPProxy");
    _priority = wxAtoi(node->GetAttribute("Priority", "100"));
    _dirty = false;
}

ControllerEthernet::ControllerEthernet(OutputManager* om, bool acceptDuplicates) : Controller(om)
{
    _managed = !acceptDuplicates;
    InitialiseTypes(false);
    _name = om->UniqueName("Ethernet_");
    _type = OUTPUT_E131;
    E131Output* o = new E131Output();
    _outputs.push_back(o);
}

void ControllerEthernet::Convert(wxXmlNode* node, std::string showDir)
{
    _outputs.push_back(Output::Create(node, showDir));
    if (_outputs.back() == nullptr)
    {
        // this shouldnt happen unless we are loading a future file with an output type we dont recognise
        _outputs.pop_back();
        return;
    }

    Controller::Convert(node, showDir);

    if (_outputs.size() == 1)
    {
        if (_name == "" || StartsWith(_name, "Ethernet_"))
        {
            if (_outputs.back()->GetDescription() != "")
            {
                _name = _outputManager->UniqueName(_outputs.back()->GetDescription());
            }
            else
            {
                _name = _outputManager->UniqueName("Unnamed");
            }
        }
        _ip = _outputs.front()->GetIP();
        _type = _outputs.front()->GetType();
        _fppProxy = _outputs.front()->GetFPPProxyIP();
        _id = _outputs.front()->GetUniverse();
        if (_id < 0) _id = _outputManager->UniqueId();
        if (_type == OUTPUT_E131)
        {
            _priority = dynamic_cast<E131Output*>(_outputs.front())->GetPriority();
        }
        else if (_type == OUTPUT_ZCPP)
        {
            _priority = dynamic_cast<ZCPPOutput*>(_outputs.front())->GetPriority();
        }
    }

    if (_outputs.back()->IsOutputCollection())
    {
        auto o = _outputs.back();
        _outputs.pop_back();

        for (auto& it : o->GetOutputs())
        {
            if (it->GetType() == OUTPUT_E131)
            {
                _outputs.push_back(new E131Output(*dynamic_cast<E131Output*>(it)));
            }
            else
            {
                wxASSERT(false);
            }
        }
        delete o;
    }
}

void ControllerEthernet::SetIP(const std::string& ip) {
    auto iip = IPOutput::CleanupIP(ip);
    if (_ip != iip) {
        _ip = iip;
        _resolvedIp = IPOutput::ResolveIP(_ip);
        _dirty = true;
        _outputManager->UpdateUnmanaged();
    }
}

void ControllerEthernet::SetProtocol(const std::string& protocol)
{
    int totchannels = GetChannels();
    auto oldtype = _type;
    auto oldoutputs = _outputs;
    int size = _outputs.size();
    _outputs.clear(); // empties the list but doesnt delete anything yet

    _type = protocol;

    if (_type == OUTPUT_ZCPP || _type == OUTPUT_DDP)
    {
        if (_type == OUTPUT_ZCPP)
        {
            _outputs.push_back(new ZCPPOutput());
        }
        else if (_type == OUTPUT_DDP)
        {
            _outputs.push_back(new DDPOutput());
        }
        _outputs.front()->SetAutoSize(oldoutputs.front()->IsAutoSize());
        _outputs.front()->SetChannels(totchannels);
        _outputs.front()->SetFPPProxyIP(oldoutputs.front()->GetFPPProxyIP());
        _outputs.front()->SetIP(oldoutputs.front()->GetIP());
        _outputs.front()->SetSuppressDuplicateFrames(oldoutputs.front()->IsSuppressDuplicateFrames());
    }
    else
    {
        if (oldtype == OUTPUT_E131 || oldtype == OUTPUT_ARTNET || oldtype == OUTPUT_xxxETHERNET)
        {
            for (const auto& it : oldoutputs)
            {
                if (_type == OUTPUT_E131)
                {
                    _outputs.push_back(new E131Output());
                }
                else if (_type == OUTPUT_ARTNET)
                {
                    _outputs.push_back(new ArtNetOutput());
                }
                else if (_type == OUTPUT_xxxETHERNET)
                {
                    _outputs.push_back(new xxxEthernetOutput());
                }
                _outputs.back()->SetIP(oldoutputs.front()->GetIP());
                _outputs.back()->SetUniverse(it->GetUniverse());
                _outputs.back()->SetChannels(it->GetChannels());
            }
        }
        else
        {
#define CONVERT_CHANNELS_PER_UNIVERSE 510
            int universes = (totchannels + CONVERT_CHANNELS_PER_UNIVERSE - 1) / CONVERT_CHANNELS_PER_UNIVERSE;
            int left = totchannels;
            for (int i = 0; i < universes; i++)
            {
                if (_type == OUTPUT_E131)
                {
                    _outputs.push_back(new E131Output());
                }
                else if (_type == OUTPUT_ARTNET)
                {
                    _outputs.push_back(new ArtNetOutput());
                }
                else if (_type == OUTPUT_xxxETHERNET)
                {
                    _outputs.push_back(new xxxEthernetOutput());
                }
                _outputs.back()->SetChannels(left > CONVERT_CHANNELS_PER_UNIVERSE ? CONVERT_CHANNELS_PER_UNIVERSE : left);
                left -= _outputs.back()->GetChannels();
                _outputs.back()->SetIP(oldoutputs.front()->GetIP());
                _outputs.back()->SetUniverse(i + 1);
            }
        }
    }

    if (_ip == "MULTICAST" && _type != OUTPUT_E131 && _type != OUTPUT_ZCPP)
    {
        SetIP("");
    }

    while (oldoutputs.size() > 0)
    {
        delete oldoutputs.front();
        oldoutputs.pop_front();
    }
}
