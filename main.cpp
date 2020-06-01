#include <SFML/Network.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <unordered_map>
#include <atomic>
#include <set>
#include <mutex>

using namespace sf;
using namespace std;

set<int> ids;
unsigned short porta = 1000;

TcpListener novaconexao;
SocketSelector atividade;
atomic<bool> running=true;
int maximo=0;
std::mutex trava;

int novo_id()
{
    for(int i=0; i<maximo; i++)
    {
        if(ids.find(i)==ids.end())
        {
            ids.insert(i);
            return i;
        }
    }
    ids.insert(maximo);
    return maximo++;
}
class Cliente
{
public:
    TcpSocket soquete;
    int tentativas=0;
    std::wstring nome;
};
unordered_map<int,unique_ptr<Cliente>> clientes;
vector<unordered_map<int,unique_ptr<Cliente>>::iterator> remover;
void checaconexao()
{
    int posi;
    while(running)
    {
        posi=0;
        sleep(seconds(10.f));
        trava.lock();
        for(auto x=clientes.begin(); x!=clientes.end(); ++x)
        {
            x->second->tentativas++;
            if(x->second->tentativas>3)
                remover.push_back(x);
            posi++;
        }
        bool removeu=false;
        for(auto x:remover)
        {
            atividade.remove(x->second->soquete);
            ids.erase(x->first);
            if(x->first+1==maximo)
                maximo--;
            clientes.erase(x);
            removeu=true;
        }
        if(removeu&&clientes.size()==0)
        {
            running=false;
            trava.unlock();
            return;
        }
        trava.unlock();
        remover.clear();
    }
}
bool arrumapacote(Packet &pacotein,Packet &pacoteout,int id)
{
    int tipo,dest;
    bool conhecido;
    pacotein>>tipo;
    switch(tipo)
    {
    case 1:
    {
        //texto
        wstring s;
        int linhas;
        pacotein>>dest>>conhecido;
        auto itenvio=clientes.find(dest);
        if(itenvio==clientes.end())
        {
            pacoteout<<-1<<dest;
            return 0;
        }
        if(!conhecido)
        {
            sf::Packet pct;
            pct<<7<<dest<<clientes[dest]->nome;
            clientes[id]->soquete.send(pct);
            std::wcout<<L"enviou o nome:"<<clientes[dest]->nome<<endl;
        }
        pacotein>>linhas>>s;
        pacoteout<<tipo<<id;
        pacoteout<<linhas<<s;
        itenvio->second->soquete.send(pacoteout);
        return 1;
        break;
    }
    case 2:
    {
        //som
        sf::Uint64 samplecount;
        unsigned int channelcount;
        unsigned int samplerate;
        int offset;
        pacotein>>dest>>conhecido;
        if(!conhecido)
        {
            sf::Packet pct;
            pct<<7<<dest<<clientes[dest]->nome;
            clientes[id]->soquete.send(pct);
        }
        auto itenvio=clientes.find(dest);
        if(itenvio==clientes.end())
        {
            pacoteout<<-1<<dest;
            return 0;
        }
        pacotein>>offset>>samplecount>>channelcount>>samplerate;
        pacoteout<<tipo<<id<<offset<<samplecount<<channelcount<<samplerate;
        sf::Uint16 sample;
        for(sf::Uint64 i=0; i<samplecount; i++)
        {
            pacotein>>sample;
            pacoteout<<sample;
        }
        std::wstring s;
        pacotein>>s;
        pacoteout<<s;

        itenvio->second->soquete.send(pacoteout);
        return 1;
        break;
    }
    case 3:
    {
        //imagempura
        pacotein>>dest>>conhecido;
        if(!conhecido)
        {
            sf::Packet pct;
            pct<<7<<dest<<clientes[dest]->nome;
            clientes[id]->soquete.send(pct);
        }
        auto itenvio=clientes.find(dest);
        if(itenvio==clientes.end())
        {
            pacoteout<<-1<<dest;
            return 0;
        }
        pacoteout<<tipo<<id;
        sf::Uint64 tam;
        sf::Uint8 in;
        float x,y;
        int offset;
        pacotein>>offset>>tam>>x>>y;
        pacoteout<<offset<<tam<<x<<y;
        for(sf::Uint64 i=0; i<tam; i++)
        {
            pacotein>>in;
            pacoteout<<in;
        }

        itenvio->second->soquete.send(pacoteout);
        return 1;
        break;
    }
    case 4:
    {
        //imagemtexto
        pacotein>>dest>>conhecido;
        if(!conhecido)
        {
            sf::Packet pct;
            pct<<7<<dest<<clientes[dest]->nome;
            clientes[id]->soquete.send(pct);
        }
        auto itenvio=clientes.find(dest);
        if(itenvio==clientes.end())
        {
            pacoteout<<-1<<dest;
            return 0;
        }
        pacoteout<<tipo<<id;
        sf::Uint64 tam;
        sf::Uint8 in;
        int offset;
        float x,y;
        wstring s;
        int linhas;
        pacotein>>offset>>tam>>x>>y;
        pacoteout<<offset<<tam<<x<<y;
        for(sf::Uint64 i=0; i<tam; i++)
        {
            pacotein>>in;
            pacoteout<<in;
        }
        pacotein>>linhas>>s;
        pacoteout<<linhas<<s;

        itenvio->second->soquete.send(pacoteout);
        return 1;
        break;
    }
    case 5:
    {
        //confirmacao
        auto itenvio=clientes.find(id);
        itenvio->second->tentativas-=3;
        if(itenvio->second->tentativas<0)
            itenvio->second->tentativas=0;
        return 1;
        break;
    }
    case 6:
    {
        //finaliza
        clientes[id]->tentativas=15;
        return 1;
        break;
    }
    case 7:
    {
        //envia nome
        int novonome;
        pacotein>>novonome;
        if(clientes.count(novonome)==0)
            pacoteout<<-1<<novonome;
        else
            pacoteout<<7<<novonome<<clientes[novonome]->nome;
        return 0;
    }
    default:
    {
        return 1;
        break;
    }
    }
}
void checasocket()
{
    if(atividade.isReady(novaconexao))
    {
        //cout<<"procou no listener\n";
        auto novocliente=make_unique<Cliente>();
        if(novaconexao.accept(novocliente->soquete)==Socket::Done)
        {
            trava.lock();
            if(!running)
            {
                trava.unlock();
                return;
            }
            atividade.add(novocliente->soquete);
            sf::Packet pacote,pacote2;
            int id=novo_id();
            pacote<<id;
            novocliente->soquete.send(pacote);
            novocliente->soquete.receive(pacote2);
            std::wstring n;
            pacote2>>n;
            novocliente->nome=n;
            clientes[id]=std::move(novocliente);
            trava.unlock();
            return;
        }
    }
    //cout<<"procou no cliente\n";
    trava.lock();
    if(!running)
    {
        trava.unlock();
        return;
    }
    for(auto &x:clientes)
    {
        if(atividade.isReady(x.second->soquete))
        {
            Packet pacote,pacote2;
            if(x.second->soquete.receive(pacote) == Socket::Done)
            {
                if(!arrumapacote(pacote,pacote2,x.first))
                {
                    x.second->soquete.send(pacote2);
                }
            }
        }
    }
    trava.unlock();
}
int main()
{
    try
    {
        novaconexao.listen(porta);
        atividade.add(novaconexao);
        cout<<IpAddress::getLocalAddress()<<endl;
        cout<<IpAddress::getPublicAddress()<<endl;
        thread t1(checaconexao);
        t1.detach();
        while(running)
        {
            if(atividade.wait(seconds(5.f)))
            {
                checasocket();
            }
        }
    }
    catch(exception &e)
    {
        cout<<e.what()<<endl;
    }
    sleep(seconds(3.f));
    return 0;
}
