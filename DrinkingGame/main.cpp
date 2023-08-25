#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <random>


class UniformRandInt {
public:
    void Init(int min, int max) {
        randEngine.seed(randDevice());
        distro = std::uniform_int_distribution<int>(min, max);
    }

    int operator()() {
        return distro(randEngine);
    }

private:
    std::random_device randDevice;
    std::mt19937 randEngine;
    std::uniform_int_distribution<int> distro;
};

enum class ResourceType 
{
    Unknown,
    Bottle,
    Opener
};

struct Resource 
{
    int id;
    int useCount;
    int lockCount;
    ResourceType type;
    std::mutex resourceMutex;
    Resource(int id_, ResourceType type_) : id(id_), type(type_), lockCount(0), useCount(0) {}
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;
};

struct DrinkerPool 
{
    int totalDrinkers;
    int drinkerCount;
    bool startingGunFlag;
    bool stopDrinkingFlag;
    std::mutex drinkerCountMutex;
    std::condition_variable drinkerCountCondition;
    std::mutex startingGunMutex;
    std::condition_variable startingGunCondition;
    std::vector<struct Drinker> drinkers;
};

struct ResourcePool 
{
    int totalResources;
    std::vector<Resource> resources;
};
struct Drinker;

struct Drinker {
    int id;
    int drinkCount;
    int resourceTryCount;
    Resource* bottle;
    Resource* opener;
    DrinkerPool* drinkerPool;
    ResourcePool* resourcePool;
    UniformRandInt myRand;
    Drinker(int id_, DrinkerPool* drinkerPool_, ResourcePool* resourcePool_)
        : id(id_), drinkCount(0), resourceTryCount(0), bottle(nullptr), opener(nullptr),
        drinkerPool(drinkerPool_), resourcePool(resourcePool_) {
        myRand.Init(0, INT_MAX);
    }
    Drinker(const Drinker&) = delete;
    Drinker& operator=(const Drinker&) = delete;
};
void WaitForAllDrinkersToBeReady(DrinkerPool& poolOfDrinkers)
{
    std::unique_lock<std::mutex> lock(poolOfDrinkers.drinkerCountMutex);
    while (poolOfDrinkers.drinkerCount < poolOfDrinkers.totalDrinkers)
    {
        poolOfDrinkers.drinkerCountCondition.wait(lock);
    }
}


void SetStopDrinkingFlag(DrinkerPool& poolOfDrinkers)
{
    std::lock_guard<std::mutex> lock(poolOfDrinkers.drinkerCountMutex);
    poolOfDrinkers.stopDrinkingFlag = true;
}


void WaitForAllDrinkersToFinish(DrinkerPool& poolOfDrinkers)
{
    std::unique_lock<std::mutex> lock(poolOfDrinkers.drinkerCountMutex);
    while (poolOfDrinkers.drinkerCount > 0)
    {
        poolOfDrinkers.drinkerCountCondition.wait(lock);
    }
}



void Pause()
{
    std::cout << "Press Enter to continue" << std::endl;
    std::cin.get();
}

void Drink(Drinker* currentDrinker)
{
    int drinkTime = 20 + (currentDrinker->myRand() % 20);
    int drunkTime = 40 + (currentDrinker->myRand() % 10);
    int bathroomTime = 60 + (currentDrinker->myRand() % 10);

    currentDrinker->bottle->useCount++;
    currentDrinker->opener->useCount++;

    std::this_thread::sleep_for(std::chrono::milliseconds(drinkTime));

    currentDrinker->bottle->resourceMutex.unlock();
    currentDrinker->opener->resourceMutex.unlock();
    currentDrinker->bottle = nullptr;
    currentDrinker->opener = nullptr;
    currentDrinker->drinkCount++;

    if ((currentDrinker->drinkCount % 5) == 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(drunkTime));
    }
    else if ((currentDrinker->drinkCount % 10) == 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(bathroomTime));
    }
}

bool TryToGetResources(Drinker* currentDrinker) 
{
    int totalResources = currentDrinker->resourcePool->totalResources;
    int trying = currentDrinker->myRand() % totalResources;

    Resource* firstResource = &currentDrinker->resourcePool->resources[trying];
    firstResource->resourceMutex.lock();
    firstResource->lockCount++;

    if (firstResource->type == ResourceType::Bottle) {
        currentDrinker->bottle = firstResource;
    }
    else {
        currentDrinker->opener = firstResource;
    }

    for (int i = 0; i < totalResources; ++i) {
        Resource* secondResource = &currentDrinker->resourcePool->resources[i];
        if (secondResource != firstResource && secondResource->type != firstResource->type) {
            if (secondResource->resourceMutex.try_lock()) {
                secondResource->lockCount++;
                if (secondResource->type == ResourceType::Bottle) {
                    currentDrinker->bottle = secondResource;
                }
                else {
                    currentDrinker->opener = secondResource;
                }
                return true;
            }
        }
    }

    firstResource->resourceMutex.unlock();
    return false;
}

bool TryToDrink(Drinker* currentDrinker)
{
    bool wasAbleToDrink = false;
    if (TryToGetResources(currentDrinker))
    {
        Drink(currentDrinker);
        wasAbleToDrink = true;
    }

    return wasAbleToDrink;
}

void StartDrinker(Drinker* currentDrinker) 
{
    while (true) {
        TryToDrink(currentDrinker);

        if (currentDrinker->drinkerPool->stopDrinkingFlag) {
            break;
        }
    }
}
void DrinkerThreadEntrypoint(Drinker* currentDrinker) 
{
    std::cout << "Drinker thread " << currentDrinker->id << " starting" << std::endl;

    int totalResources = currentDrinker->resourcePool->totalResources;
    DrinkerPool* drinkerPool = currentDrinker->drinkerPool;

    // Signal that this drinker is ready
    {
        std::lock_guard<std::mutex> lock(drinkerPool->drinkerCountMutex);
        drinkerPool->drinkerCount++;
        if (drinkerPool->drinkerCount == drinkerPool->totalDrinkers) {
            drinkerPool->startingGunCondition.notify_one();
        }
    }

    // Wait for the starting signal
    {
        std::unique_lock<std::mutex> lock(drinkerPool->startingGunMutex);
        while (!drinkerPool->startingGunFlag) {
            drinkerPool->startingGunCondition.wait(lock);
        }
    }

    StartDrinker(currentDrinker);

    // Signal that this drinker is done
    {
        std::lock_guard<std::mutex> lock(drinkerPool->drinkerCountMutex);
        drinkerPool->drinkerCount--;
        if (drinkerPool->drinkerCount == 0) {
            drinkerPool->drinkerCountCondition.notify_one();
        }
    }

    std::cout << "Drinker thread " << currentDrinker->id << " stopping" << std::endl;
}


void PrintResults(const DrinkerPool& poolOfDrinkers, const ResourcePool& poolOfResources)
{
    int resourceUseCount = 0;
    int resourceLockCount = 0;
    int drinkCount = 0;
    int resourceTryCount = 0;

    std::cout << "*********Drinkers**********" << std::endl;
    for (int i = 0; i < poolOfDrinkers.totalDrinkers; i++)
    {
        std::cout << "Drinker " << poolOfDrinkers.drinkers[i].id << ", Drank " << poolOfDrinkers.drinkers[i].drinkCount << ", " << poolOfDrinkers.drinkers[i].resourceTryCount << " tries" << std::endl;
        drinkCount += poolOfDrinkers.drinkers[i].drinkCount;
        resourceTryCount += poolOfDrinkers.drinkers[i].resourceTryCount;
    }
    std::cout << "Total Drinkers " << poolOfDrinkers.totalDrinkers << ", Drinks " << drinkCount << ", Resource tries " << resourceTryCount << std::endl << std::endl << std::endl;

    std::cout << "*********Resource Results **********" << std::endl;
    for (int i = 0; i < poolOfResources.totalResources; i++)
    {
        std::cout << "Resource " << poolOfResources.resources[i].id << " - type:" << ((poolOfResources.resources[i].type == ResourceType::Bottle) ? "bottle" : "opener") << " , locked " << poolOfResources.resources[i].lockCount << ", used " << poolOfResources.resources[i].useCount << std::endl;
        resourceUseCount += poolOfResources.resources[i].useCount;
        resourceLockCount += poolOfResources.resources[i].lockCount;
    }

    std::cout << "Total Resources = " << poolOfResources.totalResources << ", " << resourceUseCount << " use count, " << resourceLockCount << " locked count" << std::endl << std::endl << std::endl;
}

int main(int argc, char** argv)
{
    int resourceCount;
    int bottleCount;
    int openerCount;
    int drinkerCount;
    DrinkerPool poolOfDrinkers;
    ResourcePool poolOfResources;

    if (argc != 4)
    {
        std::cerr << "Usage: DrinkingGame drinkerCount bottleCount openerCount" << std::endl << std::endl;
        std::cerr << "Arguments:" << std::endl;
        std::cerr << "    drinkerCount                 Number of drinkers." << std::endl;
        std::cerr << "    bottleCount                  Number of bottles." << std::endl;
        std::cerr << "    openerCount                  Number of openers." << std::endl;
        Pause();
        return 1;
    }

    drinkerCount = atoi(argv[1]);
    bottleCount = atoi(argv[2]);
    openerCount = atoi(argv[3]);
    resourceCount = bottleCount + openerCount;

    
    poolOfResources.resources.reserve(resourceCount);
    poolOfDrinkers.drinkers.reserve(drinkerCount);

    
    for (int i = 0; i < resourceCount; i++) {
        ResourceType type = (i < bottleCount) ? ResourceType::Bottle : ResourceType::Opener;
        poolOfResources.resources.emplace_back(i, type);
    }

    // Initialize individual drinkers
    for (int i = 0; i < drinkerCount; i++) {
        poolOfDrinkers.drinkers.emplace_back(i, &poolOfDrinkers, &poolOfResources);
    }


    if (drinkerCount < 0 || bottleCount < 0 || openerCount < 0)
    {
        std::cerr << "Error: All arguments must be positive integer values." << std::endl;
        Pause();
        return 1;
    }
    if (resourceCount == 0)
    {
        std::cerr << "Error: Requires at least one resource." << std::endl;
        Pause();
        return 1;
    }

    std::cout << argv[0] << " starting " << drinkerCount << " drinker(s), " << bottleCount << " bottle(s), " << openerCount << " opener(s)" << std::endl;


    // Initialize drinker pool
    poolOfDrinkers.totalDrinkers = drinkerCount;
    poolOfDrinkers.drinkerCount = 0;
    poolOfDrinkers.startingGunFlag = false;
    poolOfDrinkers.stopDrinkingFlag = false;
    poolOfDrinkers.drinkers.resize(drinkerCount); // Use resize instead of new

    // Initialize resource pool
    poolOfResources.totalResources = resourceCount;
    poolOfResources.resources.resize(resourceCount);
    // Initialize individual resources
   // Initialize individual resources
    for (int i = 0; i < resourceCount; i++) {
        ResourceType type = (i < bottleCount) ? ResourceType::Bottle : ResourceType::Opener;
        poolOfResources.resources.emplace_back(i, type); // Use emplace_back to construct the object in-place
    }


   // Initialize individual drinkers
    for (int i = 0; i < drinkerCount; i++) {
        poolOfDrinkers.drinkers.emplace_back(i, &poolOfDrinkers, &poolOfResources); // Use emplace_back to construct the object in-place
    }


    // Create drinker threads
    std::vector<std::thread> drinkerThreads(drinkerCount); // Use vector of threads
    for (int i = 0; i < drinkerCount; i++) {
        drinkerThreads[i] = std::thread(DrinkerThreadEntrypoint, &poolOfDrinkers.drinkers[i]);
    }

    // Wait for all drinkers to be ready
    WaitForAllDrinkersToBeReady(poolOfDrinkers);
    std::cout << "Main: Firing gun" << std::endl;
    std::cout.flush();


    // Start all of the drinkers
    poolOfDrinkers.startingGunFlag = true;
    poolOfDrinkers.startingGunCondition.notify_all();

    // Wait for user input before telling the drinkers to stop
    Pause();

    // Set the stopDrinkingFlag so the drinkers break out of their drinking loop
    SetStopDrinkingFlag(poolOfDrinkers);

    // Wait for all drinker threads to finish
    for (int i = 0; i < drinkerCount; i++) {
        drinkerThreads[i].join();
    }

    PrintResults(poolOfDrinkers, poolOfResources);

    


    return 0;
}
